/* Created 19 Nov 2016 by Chris Osborn <fozztexx@fozztexx.com>
 * http://insentricity.com
 *
 * This is a driver for the WS2812 RGB LEDs using the RMT peripheral on the ESP32.
 *
 * This code is placed in the public domain (or CC0 licensed, at your option).
 *
 * Adapted for Lua-RTOS-ESP32 by LoBo (loboris@gmail.com)
 */

#ifndef WS2812_DRIVER_H
#define WS2812_DRIVER_H

#include <stdint.h>
#include <sys/driver.h>
#include <drivers/cpu.h>

// Resources used by ONE WIRE
typedef struct {
	uint8_t pin;
	uint8_t rmtchannel;
} ws2812_resources_t;

// WS2812 driver errors
#define WS2812_ERR_CANT_INIT                (DRIVER_EXCEPTION_BASE(OWIRE_DRIVER_ID) |  0)
#define WS2812_ERR_INVALID_CHANNEL          (DRIVER_EXCEPTION_BASE(OWIRE_DRIVER_ID) |  1)


typedef union {
  struct __attribute__ ((packed)) {
    uint8_t r, g, b;
  };
  uint32_t num;
} rgbVal;

extern void ws2812_init(int gpioNum);
extern void ws2812_setColors(unsigned int length, rgbVal *array);

inline rgbVal makeRGBVal(uint8_t r, uint8_t g, uint8_t b)
{
  rgbVal v;


  v.r = r;
  v.g = g;
  v.b = b;
  return v;
}

driver_error_t *ws2812_setup_pin(int8_t pin);

#endif /* WS2812_DRIVER_H */
