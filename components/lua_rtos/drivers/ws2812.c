/* Created 19 Nov 2016 by Chris Osborn <fozztexx@fozztexx.com>
 * http://insentricity.com
 *
 * Uses the RMT peripheral on the ESP32 for very accurate timing of
 * signals sent to the WS2812 LEDs.
 *
 * This code is placed in the public domain (or CC0 licensed, at your option).
 *
 * Adapted for Lua-RTOS-ESP32 by LoBo (loboris@gmail.com)
 */

#include "ws2812.h"
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

#define ETS_RMT_CTRL_INUM	18
#define ESP_RMT_CTRL_DISABLE	ESP_RMT_CTRL_DIABLE /* Typo in esp_intr.h */

#define WS2812_CYCLE	230 /* nanoseconds */
#define RESET		50000 /* nanoseconds */
#define DURATION	11.5 /* minimum time of a single RMT duration
				in nanoseconds based on clock */
#define DIVIDER		1 /* Any other values cause flickering */
#define PULSE		((WS2812_CYCLE * 2) / (DURATION * DIVIDER))
#define MAX_PULSES	32

#define RMTCHANNEL	0

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
static unsigned int ws2812_pos, ws2812_len, ws2812_half;
static xSemaphoreHandle ws2812_sem = NULL;
static rmtPulsePair ws2812_bits[2];


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

// Get the pins used by an ONE WIRE channel
void ws2812_pins(int8_t wspin, uint8_t *pin) {
	if ((wspin >= WS2812_FIRST_PIN) && (wspin <= WS2812_LAST_PIN)) *pin = wspin;
}

// Lock resources needed by ONE WIRE
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

// Setup an ONE WIRE channel
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


void ws2812_initRMTChannel(int rmtChannel)
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

void ws2812_copy()
{
  unsigned int i, j, offset, len, bit;


  offset = ws2812_half * MAX_PULSES;
  ws2812_half = !ws2812_half;

  len = ws2812_len - ws2812_pos;
  if (len > (MAX_PULSES / 8))
    len = (MAX_PULSES / 8);

  if (!len) {
    for (i = 0; i < MAX_PULSES; i++)
      RMTMEM.chan[RMTCHANNEL].data32[i + offset].val = 0;
    return;
  }

  for (i = 0; i < len; i++) {
    bit = ws2812_buffer[i + ws2812_pos];
    for (j = 0; j < 8; j++, bit <<= 1) {
      RMTMEM.chan[RMTCHANNEL].data32[j + i * 8 + offset].val =
	ws2812_bits[(bit >> 7) & 0x01].val;
    }
    if (i + ws2812_pos == ws2812_len - 1)
      RMTMEM.chan[RMTCHANNEL].data32[7 + i * 8 + offset].duration1 += RESET / DURATION;
  }

  for (i *= 8; i < MAX_PULSES; i++)
    RMTMEM.chan[RMTCHANNEL].data32[i + offset].val = 0;

  ws2812_pos += len;
  return;
}

void ws2812_handleInterrupt(void *arg)
{
  portBASE_TYPE taskAwoken = 0;


  if (RMT.int_st.ch0_tx_thr_event) {
    ws2812_copy();
    RMT.int_clr.ch0_tx_thr_event = 1;
  }
  else if (RMT.int_st.ch0_tx_end && ws2812_sem) {
    xSemaphoreGiveFromISR(ws2812_sem, &taskAwoken);
    RMT.int_clr.ch0_tx_end = 1;
  }

  return;
}

void ws2812_init(int gpioNum)
{
  SET_PERI_REG_MASK(DPORT_PERIP_CLK_EN_REG, DPORT_RMT_CLK_EN);
  CLEAR_PERI_REG_MASK(DPORT_PERIP_RST_EN_REG, DPORT_RMT_RST);

  PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[gpioNum], 2);
  gpio_matrix_out(gpioNum, RMT_SIG_OUT0_IDX + RMTCHANNEL, 0, 0);
  gpio_set_direction(gpioNum, GPIO_MODE_OUTPUT);

  ws2812_initRMTChannel(RMTCHANNEL);

  RMT.tx_lim_ch[RMTCHANNEL].limit = MAX_PULSES;
  intr_matrix_set(0, ETS_RMT_INTR_SOURCE, ETS_RMT_CTRL_INUM);
  ESP_RMT_CTRL_INTRL(ws2812_handleInterrupt, NULL);
  RMT.int_ena.ch0_tx_thr_event = 1;
  RMT.int_ena.ch0_tx_end = 1;

  ws2812_bits[0].level0 = 1;
  ws2812_bits[0].level1 = 0;
  ws2812_bits[0].duration0 = ws2812_bits[0].duration1 = PULSE;
  ws2812_bits[1].level0 = 1;
  ws2812_bits[1].level1 = 0;
  ws2812_bits[1].duration0 = ws2812_bits[1].duration1 = 2 * PULSE;

  ESP_INTR_ENABLE(ETS_RMT_CTRL_INUM);

  return;
}

void ws2812_setColors(unsigned int length, rgbVal *array)
{
  unsigned int i;


  ws2812_len = (length * 3) * sizeof(uint8_t);
  ws2812_buffer = malloc(ws2812_len);

  for (i = 0; i < length; i++) {
    ws2812_buffer[0 + i * 3] = array[i].g;
    ws2812_buffer[1 + i * 3] = array[i].r;
    ws2812_buffer[2 + i * 3] = array[i].b;
  }

  ws2812_pos = 0;
  ws2812_half = 0;

  ws2812_copy();

  if (ws2812_pos < ws2812_len)
    ws2812_copy();

  ws2812_sem = xSemaphoreCreateBinary();

  RMT.conf_ch[RMTCHANNEL].conf1.mem_rd_rst = 1;
  RMT.conf_ch[RMTCHANNEL].conf1.tx_start = 1;

  xSemaphoreTake(ws2812_sem, portMAX_DELAY);
  vSemaphoreDelete(ws2812_sem);
  ws2812_sem = NULL;

  free(ws2812_buffer);

  return;
}
