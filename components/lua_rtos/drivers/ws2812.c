/*
 * A driver for the WS2812 RGB LEDs using the RMT peripheral on the ESP32.
 *
 * Modifications Copyright (c) 2017 Martin F. Falatic
 *
 * Based on public domain code created 19 Nov 2016 by Chris Osborn <fozztexx@fozztexx.com>
 * http://insentricity.com
 *
 * Adapted for Lua-RTOS-ESP32 by LoBo (loboris@gmail.com)
 */
/*
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <soc/rmt_struct.h>
#include <soc/dport_reg.h>
#include <driver/gpio.h>
#include <soc/gpio_sig_map.h>
#include <esp_intr.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/syslog.h>
#include <drivers/gpio.h>
#include "ws2812.h"

#define DEBUG 1


#define RMTCHANNEL          0 /* There are 8 possible channels */
#define DIVIDER             4 /* 8 still seems to work, but timings become marginal */
#define MAX_PULSES         32 /* A channel has a 64 "pulse" buffer - we use half per pass */
#define RMT_DURATION_NS  12.5 /* minimum time of a single RMT duration based on clock ns */

typedef struct {
  uint32_t T0H;
  uint32_t T1H;
  uint32_t T0L;
  uint32_t T1L;
  uint32_t TRS;
} timingParams;

timingParams ledParams;
timingParams ledParams_WS2812  = { .T0H = 350, .T1H = 700, .T0L = 800, .T1L = 600, .TRS =  50000};
timingParams ledParams_WS2812B = { .T0H = 350, .T1H = 900, .T0L = 900, .T1L = 350, .TRS =  50000};
timingParams ledParams_SK6812  = { .T0H = 300, .T1H = 600, .T0L = 900, .T1L = 600, .TRS =  80000};
timingParams ledParams_WS2813  = { .T0H = 350, .T1H = 800, .T0L = 350, .T1L = 350, .TRS = 300000};

typedef union {
  struct {
    uint32_t duration0:15;
    uint32_t level0:1;
    uint32_t duration1:15;
    uint32_t level1:1;
  };
  uint32_t val;
} rmtPulsePair;

static uint8_t *ws2812_buffer = NULL;
static uint16_t ws2812_pos, ws2812_len, ws2812_half, ws2812_bufIsDirty;
static xSemaphoreHandle ws2812_sem = NULL;
static intr_handle_t rmt_intr_handle = NULL;
static rmtPulsePair ws2812_bitval_to_rmt_map[2];


#define WS2812_FIRST_PIN	1
#define WS2812_LAST_PIN		31
// This macro gets a reference for this driver into drivers array
#define WS2812_DRIVER driver_get_by_name("ws2812")
//#define RMT_DRIVER driver_get_by_name("rmt")

// Driver locks
driver_unit_lock_t ws2812_locks[CPU_LAST_GPIO];

// Driver message errors
DRIVER_REGISTER_ERROR(WS2812, ws2812, CannotSetup, "can't setup", WS2812_ERR_CANT_INIT);
DRIVER_REGISTER_ERROR(WS2812, ws2812, InvalidChannel, "invalid channel", WS2812_ERR_INVALID_CHANNEL);

// Get the pins used by ws2812
//--------------------------------------------
void ws2812_pins(int8_t wspin, uint8_t *pin) {
	if ((wspin >= WS2812_FIRST_PIN) && (wspin <= WS2812_LAST_PIN)) *pin = wspin;
}

// Lock resources needed by ws2812
//------------------------------------------------------------------
driver_error_t *ws2812_lock_resources(int8_t pin, void *resources) {
	ws2812_resources_t tmp_ws2812_resources;

	if (!resources) {
		resources = &tmp_ws2812_resources;
	}

	ws2812_resources_t *ws2812_resources = (ws2812_resources_t *)resources;
    driver_unit_lock_error_t *lock_error = NULL;

    ws2812_pins(pin, &ws2812_resources->pin);
    ws2812_resources->rmtchannel = RMTCHANNEL;

    // Lock ws2812 pin
    if ((lock_error = driver_lock(WS2812_DRIVER, pin, GPIO_DRIVER, ws2812_resources->pin))) {
    	// Revoked lock on pin
    	return driver_lock_error(WS2812_DRIVER, lock_error);
    }
    // Lock ws2812 RMT CHANNEL
    /*if ((lock_error = driver_lock(WS2812_DRIVER, RMTCHANNEL, RMT_DRIVER, ws2812_resources->rmtchannel))) {
    	// Revoked lock on pin
    	return driver_lock_error(WS2812_DRIVER, lock_error);
    }*/

    return NULL;
}

// Setup ws2812 pin
//--------------------------------------------
driver_error_t *ws2812_setup_pin(int8_t pin) {
	// Sanity checks
	if ((pin < WS2812_FIRST_PIN) || (pin > WS2812_LAST_PIN)) {
		return driver_setup_error(WS2812_DRIVER, WS2812_ERR_CANT_INIT, "invalid pin");
	}

    // Lock resources
    driver_error_t *error;
    ws2812_resources_t resources;

    if ((error = ws2812_lock_resources(pin, &resources))) {
		return error;
	}

    syslog(LOG_INFO, "ws2812%d: on RMT channel %s%d", pin, gpio_portname(resources.pin), gpio_name(resources.rmtchannel));

    return NULL;
}

DRIVER_REGISTER(WS2812,ws2812,ws2812_locks,NULL,NULL);


//===============================================================================================

void initRMTChannel(int rmtChannel)
{
  RMT.apb_conf.fifo_mask = 1;  //enable memory access, instead of FIFO mode.
  RMT.apb_conf.mem_tx_wrap_en = 1; //wrap around when hitting end of buffer
  RMT.conf_ch[rmtChannel].conf0.div_cnt = DIVIDER;
  RMT.conf_ch[rmtChannel].conf0.mem_size = 1;
  RMT.conf_ch[rmtChannel].conf0.carrier_en = 0;
  RMT.conf_ch[rmtChannel].conf0.carrier_out_lv = 1;
  RMT.conf_ch[rmtChannel].conf0.mem_pd = 0;

  RMT.conf_ch[rmtChannel].conf1.rx_en = 0;
  RMT.conf_ch[rmtChannel].conf1.mem_owner = 0;
  RMT.conf_ch[rmtChannel].conf1.tx_conti_mode = 0;    //loop back mode.
  RMT.conf_ch[rmtChannel].conf1.ref_always_on = 1;    // use apb clock: 80M
  RMT.conf_ch[rmtChannel].conf1.idle_out_en = 1;
  RMT.conf_ch[rmtChannel].conf1.idle_out_lv = 0;

  return;
}

void copyToRmtBlock_half()
{
  // This fills half an RMT block
  // When wraparound is happening, we want to keep the inactive half of the RMT block filled
  uint16_t i, j, offset, len, byteval;

  offset = ws2812_half * MAX_PULSES;
  ws2812_half = !ws2812_half;

  len = ws2812_len - ws2812_pos;
  if (len > (MAX_PULSES / 8))
    len = (MAX_PULSES / 8);

  if (!len) {
    if (!ws2812_bufIsDirty) {
      return;
    }
    // Clear the channel's data block and return
    for (i = 0; i < MAX_PULSES; i++) {
      RMTMEM.chan[RMTCHANNEL].data32[i + offset].val = 0;
    }
    ws2812_bufIsDirty = 0;
    return;
  }
  ws2812_bufIsDirty = 1;

  for (i = 0; i < len; i++) {
    byteval = ws2812_buffer[i + ws2812_pos];

    #if DEBUG_WS2812_DRIVER
      snprintf(ws2812_debugBuffer, ws2812_debugBufferSz, "%s%d(", ws2812_debugBuffer, byteval);
    #endif

    // Shift bits out, MSB first, setting RMTMEM.chan[n].data32[x] to the rmtPulsePair value corresponding to the buffered bit value
    for (j = 0; j < 8; j++, byteval <<= 1) {
      int bitval = (byteval >> 7) & 0x01;
      int data32_idx = i * 8 + offset + j;
      RMTMEM.chan[RMTCHANNEL].data32[data32_idx].val = ws2812_bitval_to_rmt_map[bitval].val;
      #if DEBUG_WS2812_DRIVER
        snprintf(ws2812_debugBuffer, ws2812_debugBufferSz, "%s%d", ws2812_debugBuffer, bitval);
      #endif
    }
    #if DEBUG_WS2812_DRIVER
      snprintf(ws2812_debugBuffer, ws2812_debugBufferSz, "%s) ", ws2812_debugBuffer);
    #endif

    // Handle the reset bit by stretching duration1 for the final bit in the stream
    if (i + ws2812_pos == ws2812_len - 1) {
      RMTMEM.chan[RMTCHANNEL].data32[i * 8 + offset + 7].duration1 +=
        ledParams.TRS / (RMT_DURATION_NS * DIVIDER);
      #if DEBUG_WS2812_DRIVER
        snprintf(ws2812_debugBuffer, ws2812_debugBufferSz, "%sRESET ", ws2812_debugBuffer);
      #endif
    }
  }

  // Clear the remainder of the channel's data not set above
  for (i *= 8; i < MAX_PULSES; i++) {
    RMTMEM.chan[RMTCHANNEL].data32[i + offset].val = 0;
  }

  ws2812_pos += len;

#if DEBUG_WS2812_DRIVER
  snprintf(ws2812_debugBuffer, ws2812_debugBufferSz, "%s ", ws2812_debugBuffer);
#endif

  return;
}


void ws2812_handleInterrupt(void *arg)
{
  portBASE_TYPE taskAwoken = 0;

  if (RMT.int_st.ch0_tx_thr_event) {
    copyToRmtBlock_half();
    RMT.int_clr.ch0_tx_thr_event = 1;
  }
  else if (RMT.int_st.ch0_tx_end && ws2812_sem) {
    xSemaphoreGiveFromISR(ws2812_sem, &taskAwoken);
    RMT.int_clr.ch0_tx_end = 1;
  }

  return;
}

int ws2812_init(int gpioNum, int ledType)
{
  #if DEBUG_WS2812_DRIVER
    ws2812_debugBuffer = (char*)calloc(ws2812_debugBufferSz, sizeof(char));
  #endif

  switch (ledType) {
    case LED_WS2812:
      ledParams = ledParams_WS2812;
      break;
    case LED_WS2812B:
      ledParams = ledParams_WS2812B;
      break;
    case LED_SK6812:
      ledParams = ledParams_SK6812;
      break;
    case LED_WS2813:
      ledParams = ledParams_WS2813;
      break;
    default:
      return -1;
  }

  SET_PERI_REG_MASK(DPORT_PERIP_CLK_EN_REG, DPORT_RMT_CLK_EN);
  CLEAR_PERI_REG_MASK(DPORT_PERIP_RST_EN_REG, DPORT_RMT_RST);

  PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[gpioNum], 2);
  gpio_matrix_out((gpio_num_t)gpioNum, RMT_SIG_OUT0_IDX + RMTCHANNEL, 0, 0);
  gpio_set_direction((gpio_num_t)gpioNum, GPIO_MODE_OUTPUT);

  initRMTChannel(RMTCHANNEL);

  RMT.tx_lim_ch[RMTCHANNEL].limit = MAX_PULSES;
  RMT.int_ena.ch0_tx_thr_event = 1;
  RMT.int_ena.ch0_tx_end = 1;

  // RMT config for WS2812 bit val 0
  ws2812_bitval_to_rmt_map[0].level0 = 1;
  ws2812_bitval_to_rmt_map[0].level1 = 0;
  ws2812_bitval_to_rmt_map[0].duration0 = ledParams.T0H / (RMT_DURATION_NS * DIVIDER);
  ws2812_bitval_to_rmt_map[0].duration1 = ledParams.T0L / (RMT_DURATION_NS * DIVIDER);

  // RMT config for WS2812 bit val 1
  ws2812_bitval_to_rmt_map[1].level0 = 1;
  ws2812_bitval_to_rmt_map[1].level1 = 0;
  ws2812_bitval_to_rmt_map[1].duration0 = ledParams.T1H / (RMT_DURATION_NS * DIVIDER);
  ws2812_bitval_to_rmt_map[1].duration1 = ledParams.T1L / (RMT_DURATION_NS * DIVIDER);

  esp_intr_alloc(ETS_RMT_INTR_SOURCE, 0, ws2812_handleInterrupt, NULL, &rmt_intr_handle);

  return 0;
}

void ws2812_setColors(uint16_t length, rgbVal *array)
{
  uint16_t i;

  ws2812_len = (length * 3) * sizeof(uint8_t);
  ws2812_buffer = (uint8_t *) malloc(ws2812_len);

  for (i = 0; i < length; i++) {
    // Where color order is translated from RGB (e.g., WS2812 = GRB)
    ws2812_buffer[0 + i * 3] = array[i].g;
    ws2812_buffer[1 + i * 3] = array[i].r;
    ws2812_buffer[2 + i * 3] = array[i].b;
  }

  ws2812_pos = 0;
  ws2812_half = 0;

  copyToRmtBlock_half();

  if (ws2812_pos < ws2812_len) {
    // Fill the other half of the buffer block
    #if DEBUG_WS2812_DRIVER
      snprintf(ws2812_debugBuffer, ws2812_debugBufferSz, "%s# ", ws2812_debugBuffer);
    #endif
    copyToRmtBlock_half();
  }


  ws2812_sem = xSemaphoreCreateBinary();

  RMT.conf_ch[RMTCHANNEL].conf1.mem_rd_rst = 1;
  RMT.conf_ch[RMTCHANNEL].conf1.tx_start = 1;

  xSemaphoreTake(ws2812_sem, portMAX_DELAY);
  vSemaphoreDelete(ws2812_sem);
  ws2812_sem = NULL;

  free(ws2812_buffer);

  return;
}
