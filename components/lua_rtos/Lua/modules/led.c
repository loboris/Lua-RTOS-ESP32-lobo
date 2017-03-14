// Module for interfacing with PIO

#include "luartos.h"

#if CONFIG_LUA_RTOS_LUA_USE_LED

#include "lualib.h"
#include "lauxlib.h"
#include "auxmods.h"
#include "pio.h"
#include "modules.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include <drivers/cpu.h>
#include <drivers/gpio.h>
#include "drivers/ws2812.h"
#include <freertos/task.h>
#include "error.h"

static rgbVal *ws2812_buf = NULL;
static uint8_t ws2812_pixels = 8;


// Setup WS2812 on 'data_pin' for 'num_leds' LEDs of given type 'type'
// led.ws2812.init(data_pin, num_led, type)
//========================================
static int pio_ws2812_init(lua_State *L) {
	driver_error_t *error;

	int pin = luaL_checkinteger(L, 1);
    if ((error = ws2812_setup_pin(pin))) {
    	return luaL_driver_error(L, error);
    }

    int pcnt = luaL_checkinteger(L, 2);
    if (pcnt < 1) pcnt = 1;
    if (pcnt > 128) pcnt = 128;
    int type = luaL_checkinteger(L, 3) & 3;

    ws2812_pixels = pcnt;
    ws2812_init(pin,type);
    if (!ws2812_buf) {
    	ws2812_buf = malloc(sizeof(rgbVal) * pcnt);
    }
    if (!ws2812_buf) {
        return luaL_error(L, "error allocating buffer");
    }
    rgbVal color = makeRGBVal(0, 0, 0);
    for (uint8_t i = 0; i < ws2812_pixels; i++) {
    	ws2812_buf[i] = color;
    }
    ws2812_setColors(ws2812_pixels, ws2812_buf);

    return 0;
}

// Deinitialize LEDs, clear LUDs and free buffer
// led.ws2812.deinit()
//=========================================
static int pio_ws2812_deinit(lua_State *L) {
    if (ws2812_buf) {
        rgbVal color = makeRGBVal(0, 0, 0);
        for (uint8_t i = 0; i < ws2812_pixels; i++) {
        	ws2812_buf[i] = color;
        }
        ws2812_setColors(ws2812_pixels, ws2812_buf);
    	free(ws2812_buf);
    	ws2812_buf = NULL;
    }

    return 0;
}

// Clear LEDs
// led.ws2812.clear()
//=========================================
static int pio_ws2812_clear(lua_State *L) {
    if (ws2812_buf) {
        rgbVal color = makeRGBVal(0, 0, 0);
        for (uint8_t i = 0; i < ws2812_pixels; i++) {
        	ws2812_buf[i] = color;
        }
        ws2812_setColors(ws2812_pixels, ws2812_buf);
    }

    return 0;
}

// update LEDs from buffer
// led.ws2812.update()
//==========================================
static int pio_ws2812_update(lua_State *L) {
    if (ws2812_buf) {
        ws2812_setColors(ws2812_pixels, ws2812_buf);
    }

    return 0;
}

// Set color at pos in led buffer
// Optionally, number of LEDs to update can be given
// led.ws2812.set(pos, color | r,g,b [, num_leds])
//=============================================
static int pio_ws2812_set_color(lua_State *L) {
    if (ws2812_buf) {
        uint8_t cnt = 1;
        rgbVal color;
        uint8_t pos = luaL_checkinteger(L, 1) - 1;
        if (lua_gettop(L) > 3) {
			uint8_t r = luaL_checkinteger(L, 2) & 0xFF;
			uint8_t g = luaL_checkinteger(L, 3) & 0xFF;
			uint8_t b = luaL_checkinteger(L, 4) & 0xFF;
			if (lua_gettop(L) > 4) {
				cnt = luaL_checkinteger(L, 5) & 0xFF;
			}
			color = makeRGBVal(r, g, b);
        }
        else {
        	int clr = luaL_checkinteger(L, 2) & 0xFFFFFF;
			if (lua_gettop(L) > 2) {
				cnt = luaL_checkinteger(L, 3) & 0xFF;
			}
			color = makeRGBVal(clr >> 16, (clr >> 8) & 0xFF, clr & 0xFF);
        }
        if (pos < ws2812_pixels) {
        	if (cnt < 1) cnt = 1;
        	if ((cnt + pos) > ws2812_pixels) cnt = ws2812_pixels - pos;
        	for (uint8_t i=0;i<cnt;i++) {
        		ws2812_buf[pos+i] = color;
        	}
        }
    }

    return 0;
}

// Set color at pos in led buffer & update LEDs
// Optionally, number of LEDs to update can be given
// led.ws2812.setcol(pos, color | r,g,b [, num_leds])
//===================================================
static int pio_ws2812_setupdate_color(lua_State *L) {
	pio_ws2812_set_color(L);
	pio_ws2812_update(L);
	return 0;
}

//=============================================
static int pio_ws2812_get_color(lua_State *L) {
    if (ws2812_buf) {
        uint8_t pos = luaL_checkinteger(L, 1);
        if ((pos > 0) && (pos <= ws2812_pixels)) {
			rgbVal color = ws2812_buf[pos-1];
			lua_pushinteger(L, color.r);
			lua_pushinteger(L, color.g);
			lua_pushinteger(L, color.b);
        }
        else {
    		lua_pushinteger(L, -1);
    		lua_pushinteger(L, -1);
    		lua_pushinteger(L, -1);
        }
    }
    else {
		lua_pushinteger(L, -1);
		lua_pushinteger(L, -1);
		lua_pushinteger(L, -1);
    }

    return 3;
}

//======================================
static int pio_ws2812_test(lua_State *L)
{
    if (!ws2812_buf) {
    	return 0;
    }
    int time = luaL_checkinteger(L, 1);
    const uint8_t anim_step = 10;
    const uint8_t anim_max = 200;
    const uint8_t pixel_count = ws2812_pixels; // Number of your "pixels"
    const uint8_t delay = 25; 				   // duration between color changes
    rgbVal color = makeRGBVal(anim_max, 0, 0);
    uint8_t step = 0;
    rgbVal color2 = makeRGBVal(anim_max, 0, 0);
    uint8_t step2 = 0;

    time *= 1000;

    while (time > 0) {
      color = color2;
      step = step2;

      for (uint8_t i = 0; i < pixel_count; i++) {
        ws2812_buf[i] = color;

        if (i == 1) {
          color2 = color;
          step2 = step;
        }

        switch (step) {
        case 0:
          color.g += anim_step;
          if (color.g >= anim_max)
            step++;
          break;
        case 1:
          color.r -= anim_step;
          if (color.r == 0)
            step++;
          break;
        case 2:
          color.b += anim_step;
          if (color.b >= anim_max)
            step++;
          break;
        case 3:
          color.g -= anim_step;
          if (color.g == 0)
            step++;
          break;
        case 4:
          color.r += anim_step;
          if (color.r >= anim_max)
            step++;
          break;
        case 5:
          color.b -= anim_step;
          if (color.b == 0)
            step = 0;
          break;
        }
      }

      ws2812_setColors(pixel_count, ws2812_buf);

      vTaskDelay(delay / portTICK_RATE_MS);
      time -= delay;
    }

    return 0;
}

#include "modules.h"

static const LUA_REG_TYPE pio_ws2812_map[] = {
	{ LSTRKEY( "init"   ),  	LFUNCVAL( pio_ws2812_init      ) },
	{ LSTRKEY( "deinit" ),  	LFUNCVAL( pio_ws2812_deinit    ) },
	{ LSTRKEY( "set"    ),  	LFUNCVAL( pio_ws2812_set_color ) },
	{ LSTRKEY( "setcol" ),	    LFUNCVAL( pio_ws2812_setupdate_color ) },
	{ LSTRKEY( "get"    ),  	LFUNCVAL( pio_ws2812_get_color ) },
	{ LSTRKEY( "clear"  ),   	LFUNCVAL( pio_ws2812_clear     ) },
	{ LSTRKEY( "update" ),	    LFUNCVAL( pio_ws2812_update    ) },
	{ LSTRKEY( "test"   ),	    LFUNCVAL( pio_ws2812_test      ) },
	{ LSTRKEY( "BLACK"  ),      LINTVAL(0x000000) },
	{ LSTRKEY( "WHITE"  ),      LINTVAL(0xFFFFFF) },
	{ LSTRKEY( "RED"    ),      LINTVAL(0xFF0000) },
	{ LSTRKEY( "LIME"   ),      LINTVAL(0x00FF00) },
	{ LSTRKEY( "BLUE"   ),      LINTVAL(0x0000FF) },
	{ LSTRKEY( "YELLOW" ),      LINTVAL(0xFFFF00) },
	{ LSTRKEY( "CYAN"   ),      LINTVAL(0x00FFFF) },
	{ LSTRKEY( "MAGENTA"),      LINTVAL(0xFF00FF) },
	{ LSTRKEY( "SILVER" ),      LINTVAL(0xC0C0C0) },
	{ LSTRKEY( "GRAY"   ),      LINTVAL(0x808080) },
	{ LSTRKEY( "MAROON" ),      LINTVAL(0x800000) },
	{ LSTRKEY( "OLIVE"  ),      LINTVAL(0x808000) },
	{ LSTRKEY( "GREEN"  ),      LINTVAL(0x008000) },
	{ LSTRKEY( "PURPLE" ),      LINTVAL(0x800080) },
	{ LSTRKEY( "TEAL"   ),      LINTVAL(0x008080) },
	{ LSTRKEY( "NAVY"   ),      LINTVAL(0x000080) },
	{ LSTRKEY( "LED_WS2812"  ), LINTVAL(LED_WS2812) },
	{ LSTRKEY( "LED_WS2812B" ), LINTVAL(LED_WS2812B) },
	{ LSTRKEY( "LED_SK6812"  ), LINTVAL(LED_SK6812) },
	{ LSTRKEY( "LED_WS2813"  ), LINTVAL(LED_WS2813) },

    { LNILKEY, LNILVAL }
};

#if !LUA_USE_ROTABLE
static const LUA_REG_TYPE led_map[] = {
    { LNILKEY, LNILVAL }
};
#else
static const LUA_REG_TYPE led_map[] = {
	{ LSTRKEY( "ws2812"   ), 	   	LROVAL  ( pio_ws2812_map           ) },
    { LNILKEY, LNILVAL }
};
#endif

LUALIB_API int luaopen_led(lua_State *L) {
#if !LUA_USE_ROTABLE
#else
	return 0;
#endif
}

MODULE_REGISTER_MAPPED(LED, led, led_map, luaopen_led);

#endif
