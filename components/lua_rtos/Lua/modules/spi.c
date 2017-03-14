/*
 * Lua RTOS, SPI wrapper
 *
 * Copyright (C) 2015 - 2016
 * IBEROXARXA SERVICIOS INTEGRALES, S.L. & CSS IBÉRICA, S.L.
 *
 * Author: Jaume Olivé (jolive@iberoxarxa.com / jolive@whitecatboard.org)
 *
 * All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software
 * and its documentation for any purpose and without fee is hereby
 * granted, provided that the above copyright notice appear in all
 * copies and that both that the copyright notice and this
 * permission notice and warranty disclaimer appear in supporting
 * documentation, and that the name of the author not be used in
 * advertising or publicity pertaining to distribution of the
 * software without specific, written prior permission.
 *
 * The author disclaim all warranties with regard to this
 * software, including all implied warranties of merchantability
 * and fitness.  In no spi shall the author be liable for any
 * special, indirect or consequential damages or any damages
 * whatsoever resulting from loss of use, data or profits, whether
 * in an action of contract, negligence or other tortious action,
 * arising out of or in connection with the use or performance of
 * this software.
 */

#include "luartos.h"

#if CONFIG_LUA_RTOS_LUA_USE_SPI

#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
#include "auxmods.h"
#include "error.h"
#include "spi.h"
#include "modules.h"
#include <string.h>

#include <drivers/espi.h>

// This variables are defined at linker time
extern LUA_REG_TYPE espi_error_map[];


//--------------------------------------------------------------------------------------------------------
static esp_err_t _spi_sendrecv(espi_userdata *spi, uint8_t *outdata, uint8_t *indata, int outlen, int inlen)
{
    spi_transaction_t t;

    memset(&t, 0, sizeof(t));	//Zero out the transaction

    t.length = 8 * outlen; //spi->data_bits * outlen;
    if (outlen != inlen) t.rxlength = spi->data_bits * inlen;
    t.tx_buffer = outdata;
    t.rx_buffer = indata;

    //Transmit!
    esp_err_t err = spi_device_transmit(spi->spi, &t);

    if (inlen > 0) {
		for (int i=0; i<inlen; i++) {
			printf("%02x ", indata[i]);
		}
		printf("\r\n");
    }
    return err;
}


//--------------------------------------------------------------------------------------------------------
static esp_err_t spi_sendrecv(espi_userdata *spi, uint8_t *outdata, uint8_t *indata, int outlen, int inlen)
{
	spi_transfer_data(spi->spi, outdata, indata, outlen, inlen);
    if (inlen > 0) {
		for (int i=0; i<inlen; i++) {
			printf("%02x ", indata[i]);
		}
		printf("\r\n");
    }
    return ESP_OK;
}


/*
 * spi_instance = spi.config(spi_interface, spi.MASTER, cs, speed_KHz, bits, mode [,sdi, sdo, sck])
 */
//===================================
static int lspi_setup(lua_State* L) {
	int id, data_bits, is_master, cs, sdi=0, sdo=0, sck=0;
	uint32_t clock;
	int spi_mode = 0;

	id = luaL_checkinteger(L, 1);
	if ((id < 2) || (id > 3)) {
    	return luaL_error(L, "Only SPI2 (HSPI) and SPI3 (VSPI) can be used");
	}

	is_master = luaL_checkinteger(L, 2) == 1;
	if (!is_master) {
    	return luaL_error(L, "Only master mode supported");
	}

	cs = luaL_checkinteger(L, 3);
	clock = luaL_checkinteger(L, 4);
	data_bits = luaL_checkinteger(L, 5);
	spi_mode = luaL_checkinteger(L, 6);
	if ((spi_mode < 0) || (spi_mode > 3)) {
		return luaL_error(L, "invalid mode number, modes 0~3 supported");
	}

	if (lua_gettop(L) > 6) {
		// Get sdi, sdo,sck pins
		sdi = luaL_checkinteger(L, 7);
		sdo = luaL_checkinteger(L, 8);
		sck = luaL_checkinteger(L, 9);
	}
	else {
		// get default pins
		spi_get_native_pins(id-1, &sdi, &sdo, &sck);
	}

	espi_userdata *spi = (espi_userdata *)lua_newuserdata(L, sizeof(espi_userdata));
	memset(spi, 0, sizeof(espi_userdata));

	spi->spi = NULL;
	spi->bus = id-1;

	spi->devcfg.spics_io_num  = -1;
	spi->devcfg.spics_ext_io_num  = cs;
	spi->devcfg.spidc_io_num  = -1;
	spi->devcfg.clock_speed_hz = clock * 1000;
	spi->devcfg.mode = spi_mode;
	spi->data_bits = data_bits;
	spi->devcfg.queue_size = 3;	// We want to be able to queue 3 transactions at a time
	spi->devcfg.pre_cb = NULL;

	spi->buscfg.miso_io_num = sdi;
	spi->buscfg.mosi_io_num = sdo;
	spi->buscfg.sclk_io_num = sck;
	spi->buscfg.quadwp_io_num=-1;
	spi->buscfg.quadhd_io_num=-1;

	esp_err_t err;
	driver_error_t *error = espi_init(spi->bus, &spi->devcfg, &spi->buscfg, &spi->spi);
	if (error) {
    	return luaL_driver_error(L, error);
    }

	/*err = spi_bus_initialize(spi->bus, &spi->buscfg, 1);
	if (err) {
    	return luaL_error(L, "Error initializing spi bus (%d)", err);
	}

    err = spi_bus_add_device(spi->bus, &spi->devcfg, &spi->buscfg, &spi->spi);
	if (err) {
    	return luaL_error(L, "Error initializing spi device (%d)", err);
	}*/

    err = spi_device_select(spi->spi, 1);
	if (err) {
    	return luaL_error(L, "Error selecting spi (%d)", err);
    }
	err = spi_device_deselect(spi->spi);
	if (err) {
    	return luaL_error(L, "Error deselecting spi (%d)", err);
    }

    luaL_getmetatable(L, "spi");
    lua_setmetatable(L, -2);

	return 1;
}

//====================================
static int lspi_select(lua_State* L) {
	esp_err_t error;
    espi_userdata *spi = NULL;

    spi = (espi_userdata *)luaL_checkudata(L, 1, "spi");
    luaL_argcheck(L, spi, 1, "spi expected");

    error = spi_device_select(spi->spi, 0);
	if (error) {
    	return luaL_error(L, "Error selecting spi device (%d)", error);
    }

    return 0;
}

//======================================
static int lspi_deselect(lua_State*L ) {
	esp_err_t error;
    espi_userdata *spi = NULL;

    spi = (espi_userdata *)luaL_checkudata(L, 1, "spi");
    luaL_argcheck(L, spi, 1, "spi expected");

    error = spi_device_deselect(spi->spi);
	if (error) {
    	return luaL_error(L, "Error deselecting spi device (%d)", error);
    }

    return 0;
}

//-------------------------------------------------------
static int lspi_rw_helper( lua_State *L, int withread ) {
	esp_err_t error = ESP_OK;
	uint8_t value, outval;
	const char *sval;
	espi_userdata *spi = NULL;

	int total = lua_gettop(L), i, j;

	spi = (espi_userdata *)luaL_checkudata(L, 1, "spi");
	luaL_argcheck(L, spi, 1, "spi expected");

    error = spi_device_select(spi->spi, 0);
	if (error) {
    	return luaL_error(L, "Error selecting spi device (%d)", error);
    }
	size_t len, residx = 1;

	if (withread) lua_newtable(L);

	for (i = 2; i <= total; i++) {
		if (lua_isnumber(L, i)) {
			outval = lua_tointeger(L, i) & 0xFF;
			if (withread) {
				error = spi_sendrecv(spi, &outval, &value, 1, 1);
				lua_pushinteger(L, value);
				lua_rawseti(L, -2, residx++);
			}
			else {
				error = spi_sendrecv(spi, &outval, NULL, 1, 0);
			}
			if (error) break;
		}
		else if (lua_isstring( L, i )) {
			sval = lua_tolstring(L, i, &len);
			uint8_t rval[len];
			if (withread) {
				error = spi_sendrecv(spi, (uint8_t *)sval, rval, len, len);
			}
			else {
				error = spi_sendrecv(spi, (uint8_t *)sval, NULL, len, 0);
			}
			if ((!error) && (withread)) {
				for(j = 0; j < len; j ++) {
					lua_pushinteger(L, rval[j]);
					lua_rawseti(L, -2, residx++);
				}
			}
			if (error) break;
		}
	}

	esp_err_t err = spi_device_deselect(spi->spi);
	if (err) error = err;

    lua_pushinteger(L, error);

	return withread ? 2 : 1;
}

//===================================
static int lspi_write(lua_State* L) {
	return lspi_rw_helper(L, 0);
}

//=======================================
static int lspi_readwrite(lua_State* L) {
	return lspi_rw_helper(L, 1);
}

// Destructor
//=====================================
static int lspi_ins_gc (lua_State *L) {
    espi_userdata *udata = NULL;

    udata = (espi_userdata *)luaL_checkudata(L, 1, "spi");
	if (udata) {
	//	free(udata->instance);
	}

	return 0;
}

static int lspi_index(lua_State *L);
static int lspi_ins_index(lua_State *L);

static const LUA_REG_TYPE lspi_map[] = {
	{ LSTRKEY( "setup"     ),	 LFUNCVAL( lspi_setup     ) },
    { LNILKEY, LNILVAL }
};

static const LUA_REG_TYPE lspi_ins_map[] = {
	{ LSTRKEY( "select"    ),	 LFUNCVAL( lspi_select    ) },
	{ LSTRKEY( "deselect"  ),	 LFUNCVAL( lspi_deselect  ) },
	{ LSTRKEY( "write"     ),	 LFUNCVAL( lspi_write     ) },
	{ LSTRKEY( "readwrite" ),	 LFUNCVAL( lspi_readwrite ) },
    { LNILKEY, LNILVAL }
};

static const LUA_REG_TYPE lspi_constants_map[] = {
	{ LSTRKEY("error"       ),   LROVAL( espi_error_map )},
	{ LSTRKEY( "MASTER"     ),	 LINTVAL( 1 ) },
	{ LSTRKEY( "SLAVE"      ),	 LINTVAL( 0 ) },
	SPI_SPI0
	SPI_SPI1
	SPI_SPI2
	SPI_SPI3
	{ LNILKEY, LNILVAL }
};

static const luaL_Reg lspi_func[] = {
    { "__index", 	lspi_index },
    { NULL, NULL }
};

static const luaL_Reg lspi_ins_func[] = {
	{ "__gc"   , 	lspi_ins_gc },
    { "__index", 	lspi_ins_index },
    { NULL, NULL }
};

static int lspi_index(lua_State *L) {
	return luaR_index(L, lspi_map, lspi_constants_map);
}

static int lspi_ins_index(lua_State *L) {
	return luaR_index(L, lspi_ins_map, NULL);
}

LUALIB_API int luaopen_spi( lua_State *L ) {
	luaL_newlib(L, lspi_func);
    lua_pushvalue(L, -1);
    lua_setmetatable(L, -2);

    luaL_newmetatable(L, "spi");
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");

    luaL_setfuncs(L, lspi_ins_func, 0);
    lua_pop(L, 1);

    return 1;
}

MODULE_REGISTER_UNMAPPED(SPI, spi, luaopen_spi);

#endif

/*

 mcp3208 = spi.setup(spi.SPI2, spi.MASTER, pio.GPIO15, 1000, 1, 1, 8)
 mcp3208:select()
 mcp3208:deselect()
 */
