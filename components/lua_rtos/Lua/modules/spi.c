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

#define ESPI_MAX_TRANS_WAIT 4000

// This variables are defined at linker time
extern LUA_REG_TYPE espi_error_map[];

//------------------------------------------------------------------------------------------------------
static esp_err_t spi_queue(espi_userdata *spi, uint8_t *outdata, uint8_t *indata, int outlen, int inlen)
{
	memset(&spi->trans[spi->trans_idx], 0, sizeof(spi_transaction_t));	//Zero out the transaction

	spi->trans[spi->trans_idx].length = 8 * outlen; //spi->data_bits * outlen;
    spi->trans[spi->trans_idx].rxlength = spi->data_bits * inlen;
    spi->trans[spi->trans_idx].tx_buffer = outdata;
    spi->trans[spi->trans_idx].rx_buffer = indata;
	if (spi->duplex == 0) spi->trans[spi->trans_idx].flags = SPI_DEVICE_HALFDUPLEX;

    //Transmit!
    esp_err_t err = spi_device_queue_trans(spi->spi, &spi->trans[spi->trans_idx], ESPI_MAX_TRANS_WAIT);
    if (err == ESP_OK) spi->trans_idx++;

    return err;
}

//-----------------------------------------------------------------------------------------------------------
static esp_err_t spi_sendrecv_q(espi_userdata *spi, uint8_t *outdata, uint8_t *indata, int outlen, int inlen)
{
	spi_transaction_t t;
	memset(&t, 0, sizeof(spi_transaction_t));	//Zero out the transaction

    t.length = 8 * outlen; //spi->data_bits * outlen;
    if (outlen != inlen) t.rxlength = spi->data_bits * inlen;
    t.tx_buffer = outdata;
    t.rx_buffer = indata;
	if (spi->duplex == 0) t.flags = SPI_DEVICE_HALFDUPLEX;

    //Transmit!
    esp_err_t err = spi_device_transmit(spi->spi, &t);

    return err;
}

//--------------------------------------------------------------------------------------------------------
static esp_err_t spi_sendrecv(espi_userdata *spi, uint8_t *outdata, uint8_t *indata, int outlen, int inlen)
{
	if (spi->queued) {
		return spi_sendrecv_q(spi, outdata, indata, outlen, inlen);
	}
	spi_transfer_data(spi->spi, outdata, indata, outlen, inlen);
    return ESP_OK;
}


/*
 * --------------------------
 * Create spi device instance
 * --------------------------
 * Params:
 *   spi_interface: spi bus interface to which the device will be attached
 *                  Only SPI2 (HSPI) and SPI3 (VSPI) can be used
 *        spi_type: Only type 1 (spi.MASTER) can be used
 *              cs: CS pin for the device
 *       speed_KHz: spi clock speed in KHz
 *            bits: number of transfer bits for one transfer item
 *                  Only used in Queued/DMA transfer
 *            mode: spi mode; 0 ~ 3
 *             sdi: OPTIONAL; device's MISO pin
 *             sdo: OPTIONAL; device's MOSI pin
 *             sck: OPTIONAL; device's SCK pin
 *                  ## If sdi, sdo and sck are not given, default (hw, native) pins of the spi bus are used ##
 *          duplex: OPTIONAL; default: half duplex
 *                            1: operate in duplex mode (receive while sending)
 *                            0: operate in half duplex mode (receive after sending)
 * Returns:
 *   spi_instance
 *
 * Example
 * spi_instance = spi.config(3, spi.MASTER, 25, 10000, 8, 0)
 * spi_instance = spi.config(3, spi.MASTER, 25, 20000, 8, 0, 1)
 * spi_instance = spi.config(3, spi.MASTER, 25, 10000, 8, 0, 12, 13, 14)
 * spi_instance = spi.config(3, spi.MASTER, 25, 40000, 8, 0, 12, 13, 14, 1)
 */
//===================================
static int lspi_setup(lua_State* L) {
	int id, data_bits, is_master, cs, sdi=0, sdo=0, sck=0;
	uint32_t clock;
	int spi_mode = 0;
	int duplex = 0;

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

	if (lua_gettop(L) >= 9) {
		// Get sdi, sdo,sck pins
		sdi = luaL_checkinteger(L, 7);
		sdo = luaL_checkinteger(L, 8);
		sck = luaL_checkinteger(L, 9);
		if (lua_gettop(L) > 9) {
			duplex = luaL_checkinteger(L, 10);
			if (duplex) duplex = 1;
		}
	}
	else {
		// get default pins
		spi_get_native_pins(id-1, &sdi, &sdo, &sck);
		if (lua_gettop(L) > 6) {
			duplex = luaL_checkinteger(L, 7);
			if (duplex) duplex = 1;
		}
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
	if (duplex == 0) spi->devcfg.flags = SPI_DEVICE_HALFDUPLEX;

	spi->buscfg.miso_io_num = sdi;
	spi->buscfg.mosi_io_num = sdo;
	spi->buscfg.sclk_io_num = sck;
	spi->buscfg.quadwp_io_num=-1;
	spi->buscfg.quadhd_io_num=-1;

	spi->duplex = duplex;

	esp_err_t err;
	driver_error_t *error = espi_init(spi->bus, &spi->devcfg, &spi->buscfg, &spi->spi);
	if (error) {
    	return luaL_driver_error(L, error);
    }

	// Test select/deselect
    err = spi_device_select(spi->spi, 1);
	if (err) {
    	return luaL_error(L, "Error selecting spi (%d)", err);
    }
	err = spi_device_deselect(spi->spi);
	if (err) {
    	return luaL_error(L, "Error deselecting spi (%d)", err);
    }

    luaL_getmetatable(L, "spi.ins");
    lua_setmetatable(L, -2);

	return 1;
}

/*
 * Check or set queued transfer mode
 */
//====================================
static int lspi_queued(lua_State* L) {
    espi_userdata *spi = NULL;

    spi = (espi_userdata *)luaL_checkudata(L, 1, "spi.ins");
    luaL_argcheck(L, spi, 1, "spi expected");

    if (lua_gettop(L) > 0) {
    	int queued = luaL_checkinteger(L, 1);
    	if (queued) queued = 1;
    	spi->queued = queued;
    }

    lua_pushinteger(L, spi->queued);
    return 1;
}

/*
 * Select the device, activate CS pin
 * If the device is selected with this function, CS will NOT be deactivated
 * until spi_instance:deselect() is executed
 *
 * If the device is NOT activated with spi_instance:select() function,
 * all other functions can still be called, CS will be activated automatically
 * and DEACTIVATED after the transfer is done
 */
//====================================
static int lspi_select(lua_State* L) {
	esp_err_t error;
    espi_userdata *spi = NULL;

    spi = (espi_userdata *)luaL_checkudata(L, 1, "spi.ins");
    luaL_argcheck(L, spi, 1, "spi expected");

    error = spi_device_select(spi->spi, 0);
	if (error) {
    	return luaL_error(L, "Error selecting spi device (%d)", error);
    }
	spi->selected = 1;

    return 0;
}

/*
 * Deselect the device, deactivate CS pin
 * Only use after spi_instance:select() function
 */
//======================================
static int lspi_deselect(lua_State*L ) {
	esp_err_t error;
    espi_userdata *spi = NULL;

    spi = (espi_userdata *)luaL_checkudata(L, 1, "spi.ins");
    luaL_argcheck(L, spi, 1, "spi expected");

    error = spi_device_deselect(spi->spi);
	if (error) {
    	return luaL_error(L, "Error deselecting spi device (%d)", error);
    }
	spi->selected = 0;

    return 0;
}

//-------------------------------------------------------
static int lspi_rw_helper( lua_State *L, int withread ) {
	esp_err_t error = ESP_OK;
	uint8_t value, outval;
	const char *sval;
	espi_userdata *spi = NULL;

	int total = lua_gettop(L), i, j;

    spi = (espi_userdata *)luaL_checkudata(L, 1, "spi.ins");
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

	if (spi->selected == 0) {
		esp_err_t err = spi_device_deselect(spi->spi);
		if (err) error = err;
	}

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

//==================================
// Additional higher level functions
//==================================

/*
 * Helper function, calculate number of bytes to send,
 * or copy parameters to send buffer
 */
//---------------------------------------------------------------------
static int spi_send(lua_State* L, uint8_t *buf, int top, uint8_t pass)
{
	uint32_t argn;
    size_t datalen, i;
    int numdata;
    int count = 0;

    // 1st pass: return count of bytes to send
    // 2nd pass: put bytes to send in buffer, return count
	for (argn = 2; argn <= top; argn++) {
		// lua_isnumber() would silently convert a string of digits to an integer
		// whereas here strings are handled separately.
		if (lua_type(L, argn) == LUA_TNUMBER) {
			numdata = (int)luaL_checkinteger(L, argn);
			if ((numdata >= 0) && (numdata <= 255)) {
				if ((pass == 2) && (buf)) buf[count] = (uint8_t)numdata;
				count++;
			}
		}
		else if (lua_istable(L, argn)) {
			datalen = lua_rawlen(L, argn);
			for (i = 0; i < datalen; i++) {
				lua_rawgeti(L, argn, i + 1);
				if (lua_type(L, -1) == LUA_TNUMBER) {
					numdata = (int)luaL_checkinteger(L, -1);
					lua_pop(L, 1);
					if ((numdata >= 0) && (numdata <= 255)) {
						if ((pass == 2) && (buf)) buf[count] = (uint8_t)numdata;
						count++;
					}
				}
				else lua_pop(L, 1);
			}
		}
		else if (lua_isstring(L, argn)) {
			const char* pdata = luaL_checklstring(L, argn, &datalen);
			if (datalen > 0) {
				if ((pass == 2) && (buf)) memcpy(buf+count, pdata, datalen);
				count += datalen;
			}
		}
	}
    return count;
}

//==============================================
static int _lspi_send(lua_State* L, int queue) {
	esp_err_t error = ESP_OK;
	espi_userdata *spi = NULL;

	// Get user data
    spi = (espi_userdata *)luaL_checkudata(L, 1, "spi.ins");
    if (spi == NULL) return -9;

	if (queue) {
		// check for free transaction buffer
		if (spi->trans_idx >= ESPI_MAX_USER_TRANS) return -3;
		if (spi->queued == 0) return -8;
	}

	uint8_t *buf = NULL;

    int count = spi_send(L, buf, lua_gettop(L), 1);

    if (count > 0) {
    	buf = malloc(count);
        if (buf == NULL) {
            return -1;
        }
        // Get data to buffer
        int count2 = spi_send(L, buf, lua_gettop(L), 2);
        if (count != count2) {
        	free(buf);
        	return -2;
        }
        // Send data
        error = spi_device_select(spi->spi, 0);
    	if (error == ESP_OK) {
    		if (queue) error = spi_queue(spi, buf, NULL, count, 0);
    		else {
    			error = spi_sendrecv(spi, buf, NULL, count, 0);
        		if (spi->selected == 0) spi_device_deselect(spi->spi);
    		}
    		if (error) error = -2;
        }
    	else error = -1;

		free(buf);
    }

    lua_pushinteger(L, count);
    lua_pushinteger(L, error);
    return 0;
}

/*
 * Send data to spi device
 * Returns number of data bytes sent & error code (0 if no error)
 * numsent, err = spiinstance:send(data1, [data2], ..., [datan] )
 * data can be either a string, a table or an 8-bit number
 */
//==================================
static int lspi_send(lua_State* L) {
	int err = _lspi_send(L, 0);
    if (err == -1) {
        return luaL_error(L, "error allocating send buffer");
    }
    else if (err == -2) {
    	return luaL_error(L, "send data count error\r\n");
    }
    else if (err == -9) {
    	luaL_argcheck(L, NULL, 1, "spi expected");
    }
	return 2;
}

/*
 * Queue data to spi device
 * Returns number of data bytes queued & error code (0 if no error)
 * numsent, err = spiinstance:qsend(data1, [data2], ..., [datan] )
 * data can be either a string, a table or an 8-bit number
 */
//==================================
static int lspi_qsend(lua_State* L) {

	int err = _lspi_send(L, 1);
    if (err == -1) {
        return luaL_error(L, "error allocating send buffer");
    }
    else if (err == -2) {
    	return luaL_error(L, "send data count error");
    }
    else if (err == -3) {
    	return luaL_error(L, "no free transaction buffers");
    }
    else if (err == -8) {
    	return luaL_error(L, "not in queued transaction mode");
    }
    else if (err == -9) {
    	luaL_argcheck(L, NULL, 1, "spi expected");
    }
	return 2;
}


//===============================================
static int _lspi_receive(lua_State* L, int queue)
{
	esp_err_t error = ESP_OK;
	espi_userdata *spi = NULL;

	// Get user data
    spi = (espi_userdata *)luaL_checkudata(L, 1, "spi.ins");
    if (spi == NULL) return -9;

	if (queue) {
		// check for free transaction buffer
		if (spi->trans_idx >= ESPI_MAX_USER_TRANS) return -2;
		if (spi->queued == 0) return -8;
	}

	uint32_t size = luaL_checkinteger(L, 2);

    int i;
    int out_type = 0;
    luaL_Buffer b;
    char hbuf[4];

    uint8_t *rbuf = malloc(size);
    if (rbuf == NULL) {
        return -1;
    }

    if (queue == 0) {
		if (lua_isstring(L, 3)) {
			const char* sarg;
			size_t sarglen;
			sarg = luaL_checklstring(L, 3, &sarglen);
			if (sarglen == 2) {
				if (strstr(sarg, "*h") != NULL) out_type = 1;
				else if (strstr(sarg, "*t") != NULL) out_type = 2;
			}
		}

		if (out_type < 2) luaL_buffinit(L, &b);
		else lua_newtable(L);
    }

    // Send data
    error = spi_device_select(spi->spi, 0);
	if (error == ESP_OK) {
		if (queue) error = spi_queue(spi, NULL, rbuf, 0, size);
		else {
			error = spi_sendrecv(spi, NULL, rbuf, 0, size);
			if (spi->selected == 0) spi_device_deselect(spi->spi);
		}
		if (error) error = -2;
    }
	else error = -1;

	if ((error == ESP_OK) && (queue == 0)) {
		for (i = 0; i < size; i++) {
			if (out_type == 0) luaL_addchar(&b, rbuf[i]);
			else if (out_type == 1) {
				sprintf(hbuf, "%02x;", rbuf[i]);
				luaL_addstring(&b, hbuf);
			}
			else {
				lua_pushinteger( L, rbuf[i]);
				lua_rawseti(L,-2, i+1);
			}
		}
	}

    free(rbuf);

    if ((out_type < 2) && (queue == 0)) luaL_pushresult(&b);

    lua_pushinteger(L, error);
    return 0;
}

/*
 * Receive data from spi device
 * Returns table, string or string of hexadecimal values & error code (0 if no error)
 * rstring, err = spiinstance:receive(size)
 * rhexstr, err = spiinstance:receive(size, "*h")
 *  rtable, err = spiinstance:receive(size, "*t")
 */
//===================================
static int lspi_receive(lua_State* L)
{
	int err = _lspi_receive(L, 0);
    if (err == -1) {
        return luaL_error(L, "error allocating receive buffer");
    }
    else if (err == -9) {
    	luaL_argcheck(L, NULL, 1, "spi expected");
    }
	return 2;
}

/*
 * Queue receive requestfrom spi device
 * Returns error code (0 if no error)
 * err = spiinstance:qreceive(size)
 */
//====================================
static int lspi_qreceive(lua_State* L)
{
	int err = _lspi_receive(L, 0);
    if (err == -1) {
        return luaL_error(L, "error allocating receive buffer");
    }
    else if (err == -2) {
    	return luaL_error(L, "no free transaction buffers");
    }
    else if (err == -8) {
    	return luaL_error(L, "not in queued transaction mode");
    }
    else if (err == -9) {
    	luaL_argcheck(L, NULL, 1, "spi expected");
    }
	return 1;
}

//===================================================
static int _lspi_sendreceive(lua_State* L, int queue)
{
	esp_err_t error = ESP_OK;
	espi_userdata *spi = NULL;

	// Get user data
    spi = (espi_userdata *)luaL_checkudata(L, 1, "spi.ins");
    if (spi == NULL) return -9;

	if (queue) {
		// check for free transaction buffer
		if (spi->trans_idx >= ESPI_MAX_USER_TRANS) return -7;
		if (spi->queued == 0) return -8;
	}
	int32_t size = 0;
    int out_type = 0;
    int top = lua_gettop(L);
    if (top < 4) return -1;

    // check last parameter
    if (lua_type(L, -1) == LUA_TNUMBER) {
        size = luaL_checkinteger(L, -1);
        top--;
    }
    else if (lua_type(L, -1) == LUA_TSTRING) {
        if (top < 5) return -2;

        if (lua_type(L, -2) == LUA_TNUMBER) {
            size = luaL_checkinteger(L, -2);

            const char* sarg;
            size_t sarglen;
            sarg = luaL_checklstring(L, -1, &sarglen);
            if (sarglen == 2) {
            	if (strstr(sarg, "*h") != NULL) out_type = 1;
            	else if (strstr(sarg, "*t") != NULL) out_type = 2;
            }
            top -= 2;
        }
        else return -3;
    }
    else return -4;

    if (size < 0) size = 0;

    int i;
    luaL_Buffer b;
    char hbuf[4];

    uint8_t *txbuf = NULL;
    uint8_t *rxbuf = NULL;

    int count = spi_send(L, txbuf, top, 1);

    // allocate send and receive buffers
	if (size > 0) {
		rxbuf = malloc(size);
		if (rxbuf == NULL) return -5;
	}

	if (count > 0) {
    	txbuf = malloc(count);
        if (txbuf == NULL) {
        	if (rxbuf != NULL) free(rxbuf);
        	return -6;
        }
    }

	if (out_type < 2) luaL_buffinit(L, &b);
    else lua_newtable(L);

	// Send and receive data data
    error = spi_device_select(spi->spi, 0);
	if (error == ESP_OK) {
		if (queue) error = spi_queue(spi, txbuf, rxbuf, count, size);
		else {
			error = spi_sendrecv(spi, txbuf, rxbuf, count, size);
			if (spi->selected == 0) spi_device_deselect(spi->spi);
		}
		if (error) error = -2;
    }
	else error = -1;

	if ((error == ESP_OK) && (size > 0)) {
		for (i = 0; i < size; i++) {
			if (out_type == 0) luaL_addchar(&b, rxbuf[i]);
			else if (out_type == 1) {
				sprintf(hbuf, "%02x;", rxbuf[i]);
				luaL_addstring(&b, hbuf);
			}
			else {
				lua_pushinteger( L, rxbuf[i]);
				lua_rawseti(L,-2, i+1);
			}
		}
	}

    if (rxbuf != NULL) free(rxbuf);
    if (txbuf != NULL) free(txbuf);

    if (out_type < 2) luaL_pushresult(&b);

    lua_pushinteger(L, error);
    return 0;
}

/*
 * Send data to spi device and receive data in one transaction
 * outdata can be either a string, a table or an 8-bit number
 * Returns table, string or string of hexadecimal values & error code (0 if no error)
 * rstring, err = spiinstance:sendreceive(outdata1, [outdata2], ..., [outdatan], read_size)
 * rhexstr, err = spiinstance:sendreceive(outdata1, [outdata2], ..., [outdatan], read_size, "*h")
 *  rtable, err = spiinstance:sendreceive(outdata1, [outdata2], ..., [outdatan], read_size, "*t")
 */
//=======================================
static int lspi_sendreceive(lua_State* L)
{
	int err = _lspi_sendreceive(L, 0);
    if (err == -1) {
    	return luaL_error(L, "invalid number of arguments");
    }
    else if (err == -2) {
    	return luaL_error(L, "invalid number of arguments");
    }
    else if (err == -3) {
    	return luaL_error(L, "size argument not found");
    }
    else if (err == -4) {
    	return luaL_error(L, "size argument not found");
    }
    else if (err == -5) {
		return luaL_error(L, "error allocating receive buffer");
    }
    else if (err == -6) {
        return luaL_error(L, "error allocating send buffer");
    }
    else if (err == -9) {
    	luaL_argcheck(L, NULL, 1, "spi expected");
    }
	return 2;
}

/*
 * Queue send data and receive request to spi device
 * outdata can be either a string, a table or an 8-bit number
 * Returns error code (0 if no error)
 * err = spiinstance:qsendreceive(outdata1, [outdata2], ..., [outdatan], read_size)
 */
//========================================
static int lspi_qsendreceive(lua_State* L)
{
	int err = _lspi_sendreceive(L, 0);
    if (err == -1) {
    	return luaL_error(L, "invalid number of arguments");
    }
    else if (err == -2) {
    	return luaL_error(L, "invalid number of arguments");
    }
    else if (err == -3) {
    	return luaL_error(L, "size argument not found");
    }
    else if (err == -4) {
    	return luaL_error(L, "size argument not found");
    }
    else if (err == -5) {
		return luaL_error(L, "error allocating receive buffer");
    }
    else if (err == -6) {
        return luaL_error(L, "error allocating send buffer");
    }
    else if (err == -7) {
    	return luaL_error(L, "no free transaction buffers");
    }
    else if (err == -8) {
    	return luaL_error(L, "not in queued transaction mode");
    }
    else if (err == -9) {
    	luaL_argcheck(L, NULL, 1, "spi expected");
    }
	return 2;
}

/*
 * Get result from previously executed queued transaction requests
 * Returns: table, string or string of hexadecimal values & error code (0 if no error)
 *
 * rstring, err = spiinstance:gettransres()
 * rhexstr, err = spiinstance:gettransres("*h")
 *  rtable, err = spiinstance:gettransres("*t")
 */
//=======================================
static int lspi_gettransres(lua_State* L)
{
	espi_userdata *spi = NULL;
	// Get user data
    spi = (espi_userdata *)luaL_checkudata(L, 1, "spi.ins");
	luaL_argcheck(L, spi, 1, "spi expected");

	if (spi->queued == 0) {
    	return luaL_error(L, "not in queued transaction mode");
	}
	if (spi->trans_idx == 0) {
    	return luaL_error(L, "not in queued transaction mode");
	}

	int i, idx, rdlen, err = 0;
    esp_err_t ret;
    spi_transaction_t *rtrans;
    int out_type = 0;
    luaL_Buffer b;
    char hbuf[4];
    uint8_t *buf;

	if (lua_isstring(L, 2)) {
		const char* sarg;
		size_t sarglen;
		sarg = luaL_checklstring(L, 3, &sarglen);
		if (sarglen == 2) {
			if (strstr(sarg, "*h") != NULL) out_type = 1;
			else if (strstr(sarg, "*t") != NULL) out_type = 2;
		}
	}

	if (out_type < 2) luaL_buffinit(L, &b);
	else lua_newtable(L);

	for (idx = 0;idx < spi->trans_idx; idx++) {
        ret=spi_device_get_trans_result(spi->spi, &rtrans, ESPI_MAX_TRANS_WAIT);
        if (ret == ESP_OK) {
        	// check if transaction some read data
        	if (rtrans->rxlength) {
        		// Collect received data
        		rdlen = rtrans->rxlength / spi->data_bits;
        		buf = (uint8_t *)rtrans->rx_buffer;
        		for (i = 0; i < rdlen; i++) {
        			if (out_type == 0) luaL_addchar(&b, buf[i]);
        			else if (out_type == 1) {
        				sprintf(hbuf, "%02x;", buf[i]);
        				luaL_addstring(&b, hbuf);
        			}
        			else {
        				lua_pushinteger( L, buf[i]);
        				lua_rawseti(L,-2, i+1);
        			}
        		}

        	}
        }
        else err++;
	}
    spi->trans_idx = 0;
	memset(&spi->trans, 0, sizeof(spi->trans));

    if (out_type < 2) luaL_pushresult(&b);
    lua_pushinteger(L, err);

    return 2;
}


// Destructor
//=====================================
static int lspi_ins_gc (lua_State *L) {
    espi_userdata *udata = NULL;

    printf("lspi_ins_gc\r\n");
    udata = (espi_userdata *)luaL_checkudata(L, 1, "spi.ins");
	if (udata) {
		spi_device_deselect(udata->spi);
		driver_error_t *error = espi_deinit(udata->bus, &udata->spi);
		if (error) {
			if (error->exception == ESPI_ERR_CANT_DEINIT_DEVICE) return luaL_driver_error(L, error);
			free(error);
		}
		free(udata);
	}

	return 0;
}

static const LUA_REG_TYPE lspi_map[] = {
	{ LSTRKEY( "setup"      ),	 LFUNCVAL( lspi_setup    ) },
	{ LSTRKEY( "error"      ),   LROVAL  ( espi_error_map ) },
	{ LSTRKEY( "MASTER"     ),	 LINTVAL ( 1 ) },
	{ LSTRKEY( "SLAVE"      ),	 LINTVAL ( 0 ) },
	SPI_SPI0
	SPI_SPI1
	SPI_SPI2
	SPI_SPI3
    { LNILKEY, LNILVAL }
};

static const LUA_REG_TYPE lspi_ins_map[] = {
	{ LSTRKEY( "select"      ),	 LFUNCVAL( lspi_select      ) },
	{ LSTRKEY( "deselect"    ),	 LFUNCVAL( lspi_deselect    ) },
	{ LSTRKEY( "write"       ),	 LFUNCVAL( lspi_write       ) },
	{ LSTRKEY( "readwrite"   ),	 LFUNCVAL( lspi_readwrite   ) },
    { LSTRKEY( "send"        ),	 LFUNCVAL( lspi_send        ) },
    { LSTRKEY( "receive"     ),  LFUNCVAL( lspi_receive     ) },
    { LSTRKEY( "sendreceive" ),	 LFUNCVAL( lspi_sendreceive ) },
    { LSTRKEY( "qsend"       ),	 LFUNCVAL( lspi_qsend       ) },
    { LSTRKEY( "qreceive"    ),  LFUNCVAL( lspi_qreceive    ) },
    { LSTRKEY( "qsendreceive"),	 LFUNCVAL( lspi_qsendreceive) },
    { LSTRKEY( "gettransres" ),	 LFUNCVAL( lspi_gettransres ) },
    { LSTRKEY( "queued"      ),	 LFUNCVAL( lspi_queued      ) },
    { LSTRKEY( "__metatable" ),	 LROVAL  ( lspi_ins_map     ) },
	{ LSTRKEY( "__index"     ),  LROVAL  ( lspi_ins_map     ) },
	{ LSTRKEY( "__gc"        ),  LROVAL  ( lspi_ins_gc      ) },
    { LNILKEY, LNILVAL }
};

LUALIB_API int luaopen_spi( lua_State *L ) {
    luaL_newmetarotable(L,"spi.ins", (void *)lspi_ins_map);
    return 0;
}

MODULE_REGISTER_MAPPED(SPI, spi, lspi_map, luaopen_spi);

#endif

/*

 -------------------------------------------------
 Example: simple non queued transactions
          send one byte & receive 2 bytes response
          CS is handled by function
 -------------------------------------------------

 spidev = spi.config(spi.SPI3, spi.MASTER, pio.GPIO25, 1000, 8, 0)

 spidev:queued(0)
 hexstr, err = spidev:sendreceive(0xd0, 2, "*h")
 print(hexstr)

 ----------------------------
 Example: queued transactions
          queue send 5 bytes than queue request for receiving 8 bytes response
          wait for transactions to finish and get the result to hex string
 ----------------------------
 spidev = spi.config(spi.SPI3, spi.MASTER, pio.GPIO25, 1000, 8, 0)

 spidev:queued(1)
 spidev:select()
 numsent, err = spidev:qsend(0x07,"ABCD")
 err = spidev:qreceive(8)
 hexstr, err = spidev:gettransres("*h")
 spidev:deselect()
 spidev:queued(0)
 print(hexstr)

 */
