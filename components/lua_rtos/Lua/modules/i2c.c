/*
 * Lua RTOS, I2C Lua module
 *
 * Copyright (C) 2015 - 2017
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
 * and fitness.  In no event shall the author be liable for any
 * special, indirect or consequential damages or any damages
 * whatsoever resulting from loss of use, data or profits, whether
 * in an action of contract, negligence or other tortious action,
 * arising out of or in connection with the use or performance of
 * this software.
 */

#include "luartos.h"

#if CONFIG_LUA_RTOS_LUA_USE_I2C

#include <string.h>
#include "lua.h"
#include "error.h"
#include "lauxlib.h"
#include "i2c.h"
#include "modules.h"
#include "error.h"

#include <drivers/i2c.h>
#include <drivers/cpu.h>

extern LUA_REG_TYPE i2c_error_map[];
extern driver_message_t i2c_errors[];

typedef struct {
	int unit;
	int transaction;
} i2c_user_data_t;

static int li2c_setup( lua_State* L ) {
	driver_error_t *error;

    int id = luaL_checkinteger(L, 1);
    int mode = luaL_checkinteger(L, 2);
    int speed = luaL_checkinteger(L, 3);
    int sda = luaL_checkinteger(L, 4);
    int scl = luaL_checkinteger(L, 5);

    if ((error = i2c_setup(id, mode, speed, sda, scl, 0, 0))) {
    	return luaL_driver_error(L, error);
    }

    // Allocate userdata
    i2c_user_data_t *user_data = (i2c_user_data_t *)lua_newuserdata(L, sizeof(i2c_user_data_t));
    if (!user_data) {
       	return luaL_exception(L, I2C_ERR_NOT_ENOUGH_MEMORY);
    }

    user_data->unit = id;
    user_data->transaction = I2C_TRANSACTION_INITIALIZER;

    luaL_getmetatable(L, "i2c.trans");
    lua_setmetatable(L, -2);

    return 1;
}

static int li2c_start( lua_State* L ) {
	driver_error_t *error;
	i2c_user_data_t *user_data;

	// Get user data
	user_data = (i2c_user_data_t *)luaL_checkudata(L, 1, "i2c.trans");
    luaL_argcheck(L, user_data, 1, "i2c transaction expected");

    if ((error = i2c_start(user_data->unit, &user_data->transaction))) {
    	return luaL_driver_error(L, error);
    }

     return 0;
}

static int li2c_stop( lua_State* L ) {
	driver_error_t *error;
	i2c_user_data_t *user_data;

	// Get user data
	user_data = (i2c_user_data_t *)luaL_checkudata(L, 1, "i2c.trans");
    luaL_argcheck(L, user_data, 1, "i2c transaction expected");

    if ((error = i2c_stop(user_data->unit, &user_data->transaction))) {
    	return luaL_driver_error(L, error);
    }

    return 0;
}

static int li2c_address( lua_State* L ) {
	driver_error_t *error;
	i2c_user_data_t *user_data;

	// Get user data
	user_data = (i2c_user_data_t *)luaL_checkudata(L, 1, "i2c.trans");
    luaL_argcheck(L, user_data, 1, "i2c transaction expected");

    int address = luaL_checkinteger(L, 2);
    int read = 0;

	luaL_checktype(L, 3, LUA_TBOOLEAN);
	if (lua_toboolean(L, 3)) {
		read = 1;
	}

	if ((error = i2c_write_address(user_data->unit, &user_data->transaction, address, read))) {
    	return luaL_driver_error(L, error);
    }
    
    return 0;
}

static int li2c_read( lua_State* L ) {
	driver_error_t *error;
	i2c_user_data_t *user_data;
	char data;

	// Get user data
	user_data = (i2c_user_data_t *)luaL_checkudata(L, 1, "i2c.trans");
    luaL_argcheck(L, user_data, 1, "i2c transaction expected");

    if ((error = i2c_read(user_data->unit, &user_data->transaction, &data, 1))) {
    	return luaL_driver_error(L, error);
    }

    // We need to flush because we need to return reaad data now
    if ((error = i2c_flush(user_data->unit, &user_data->transaction, 1))) {
    	return luaL_driver_error(L, error);
    }

    lua_pushinteger(L, (int)data);

    return 1;
}

static int li2c_write(lua_State* L) {
	driver_error_t *error;
	i2c_user_data_t *user_data;

	// Get user data
	user_data = (i2c_user_data_t *)luaL_checkudata(L, 1, "i2c.trans");
    luaL_argcheck(L, user_data, 1, "i2c transaction expected");

    char data = (char)(luaL_checkinteger(L, 2) & 0xff);
    
    esp_err_t i2c_master_write_byte(i2c_cmd_handle_t cmd_handle, uint8_t data, bool ack_en);

    if ((error = i2c_write(user_data->unit, &user_data->transaction, &data, sizeof(data)))) {
    	return luaL_driver_error(L, error);
    }

    return 0;
}

//==================================
// Additional higher level functions
//==================================

//--------------------------------------------------------------------
static int i2c_send(lua_State* L, uint8_t *buf, int top, uint8_t pass)
{
	uint32_t argn;
    size_t datalen, i;
    int numdata;
    int count = 0;

    // 1st pass: return count of bytes to send
    // 2nd pass: put bytes to send in buffer, return count
	for (argn = 3; argn <= top; argn++) {
		// lua_isnumber() would silently convert a string of digits to an integer
		// whereas here strings are handled separately.
		if (lua_type(L, argn) == LUA_TNUMBER) {
			numdata = (int)luaL_checkinteger(L, argn);
			if ((numdata >= 0) && (numdata <= 255)) {
				if (pass == 2) buf[count] = (uint8_t)numdata;
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
						if (pass == 2) buf[count] = (uint8_t)numdata;
						count++;
					}
				}
				else lua_pop(L, 1);
			}
		}
		else if (lua_isstring(L, argn)) {
			const char* pdata = luaL_checklstring(L, argn, &datalen);
			if (datalen > 0) {
				if (pass == 2) memcpy(buf+count, pdata, datalen);
				count += datalen;
			}
		}
	}
    return count;
}

/*
 * Send data to i2c device
 * Returns number of data bytes sent
 * numsent = i2cinstance:send(addr, data1, [data2], ..., [datan] )
 * data can be either a string, a table or an 8-bit number
 */
//===================================
static int li2c_send(lua_State* L) {
	driver_error_t *error;
	i2c_user_data_t *user_data;

	// Get user data
	user_data = (i2c_user_data_t *)luaL_checkudata(L, 1, "i2c.trans");
    luaL_argcheck(L, user_data, 1, "i2c transaction expected");

    int addr = luaL_checkinteger(L, 2);
    uint8_t *buf = NULL;

    int count = i2c_send(L, buf, lua_gettop(L), 1);

    if (count > 0) {
    	buf = malloc(count);
        if (buf == NULL) {
            return luaL_error(L, "error allocating send buffer");
        }
        // Get data to buffer
        int count2 = i2c_send(L, buf, lua_gettop(L), 2);
        if (count != count2) {
        	free(buf);
        	return luaL_error(L, "send data count error: %d<>%d\r\n", count, count2);
        }
        // Send data
        if ((error = i2c_start(user_data->unit, &user_data->transaction))) {
    		free(buf);
    		return luaL_driver_error(L, error);
    	}
    	if ((error = i2c_write_address(user_data->unit, &user_data->transaction, addr, 0))) {
    		free(buf);
    		return luaL_driver_error(L, error);
    	}
    	if ((error = i2c_write(user_data->unit, &user_data->transaction, (char *)buf, count))) {
    		free(buf);
    		return luaL_driver_error(L, error);
    	}
    	if ((error = i2c_stop(user_data->unit, &user_data->transaction))) {
    		free(buf);
    		return luaL_driver_error(L, error);
    	}
    	free(buf);
    }

    lua_pushinteger(L, count);
    return 1;
}


/*
 * Receive data from i2c device
 * Returns table, string or string of hexadecimal values
 * rstring = i2cinstance:receive(addr, size)
 * rhexstr = i2cinstance:receive(addr, size, "*h")
 *  rtable = i2cinstance:receive(addr, size, "*t")
 */
//===================================
static int li2c_receive(lua_State* L)
{
	driver_error_t *error;
	i2c_user_data_t *user_data;

	// Get user data
	user_data = (i2c_user_data_t *)luaL_checkudata(L, 1, "i2c.trans");
    luaL_argcheck(L, user_data, 1, "i2c transaction expected");

    int addr = luaL_checkinteger(L, 2);
	uint32_t size = luaL_checkinteger(L, 3);

    int i;
    int out_type = 0;
    luaL_Buffer b;
    char hbuf[4];

    uint8_t *rbuf = malloc(size);
    if (rbuf == NULL) {
        return luaL_error(L, "error allocating receive buffer");
    }

    if (lua_isstring(L, 4)) {
        const char* sarg;
        size_t sarglen;
        sarg = luaL_checklstring(L, 4, &sarglen);
        if (sarglen == 2) {
        	if (strstr(sarg, "*h") != NULL) out_type = 1;
        	else if (strstr(sarg, "*t") != NULL) out_type = 2;
        }
    }

    if (out_type < 2) luaL_buffinit(L, &b);
    else lua_newtable(L);

    if ((error = i2c_start(user_data->unit, &user_data->transaction))) {
    	free(rbuf);
    	return luaL_driver_error(L, error);
    }
	if ((error = i2c_write_address(user_data->unit, &user_data->transaction, addr, 1))) {
    	free(rbuf);
    	return luaL_driver_error(L, error);
    }
    if ((error = i2c_read(user_data->unit, &user_data->transaction, (char *)rbuf, size))) {
    	free(rbuf);
    	return luaL_driver_error(L, error);
    }
    if ((error = i2c_stop(user_data->unit, &user_data->transaction))) {
    	free(rbuf);
    	return luaL_driver_error(L, error);
    }

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

    free(rbuf);

    if (out_type < 2) luaL_pushresult(&b);

	return 1;
}

/*
 * Send data to i2c device and receive data in one transaction
 * outdata can be either a string, a table or an 8-bit number
 * Returns table, string or string of hexadecimal values
 * rstring = i2cinstance:sendreceive(addr, outdata1, [outdata2], ..., [outdatan], read_size)
 * rhexstr = i2cinstance:sendreceive(addr, outdata1, [outdata2], ..., [outdatan], read_size, "*h")
 *  rtable = i2cinstance:sendreceive(addr, outdata1, [outdata2], ..., [outdatan], read_size, "*t")
 */
//=======================================
static int li2c_sendreceive(lua_State* L)
{
	driver_error_t *error;
	i2c_user_data_t *user_data;

	// Get user data
	user_data = (i2c_user_data_t *)luaL_checkudata(L, 1, "i2c.trans");
    luaL_argcheck(L, user_data, 1, "i2c transaction expected");

    int addr = luaL_checkinteger(L, 2);

	uint32_t size = 0;
    int out_type = 0;
    int top = lua_gettop(L);
    if (top < 4) {
    	return luaL_error(L, "invalid number of arguments");
    }

    // check last parameter
    if (lua_type(L, -1) == LUA_TNUMBER) {
        size = luaL_checkinteger(L, -1);
        top--;
    }
    else if (lua_type(L, -1) == LUA_TSTRING) {
        if (top < 5) {
        	return luaL_error(L, "invalid number of arguments");
        }

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
        else {
        	return luaL_error(L, "size argument not found");
        }
    }
    else {
    	return luaL_error(L, "size argument not found");
    }

    int i;
    luaL_Buffer b;
    char hbuf[4];

    uint8_t *buf = NULL;

    // start transaction
    if ((error = i2c_start(user_data->unit, &user_data->transaction))) {
    	return luaL_driver_error(L, error);
    }
    int count = i2c_send(L, buf, top, 1);

    if (count > 0) {
    	buf = malloc(count);
        if (buf == NULL) {
            return luaL_error(L, "error allocating send buffer");
        }
        // Get data to buffer
        int count2 = i2c_send(L, buf, top, 2);
        if (count != count2) {
        	free(buf);
        	return luaL_error(L, "send data count error: %d<>%d\r\n", count, count2);
        }
        // Send address & data
    	if ((error = i2c_write_address(user_data->unit, &user_data->transaction, addr, 0))) {
        	free(buf);
        	return luaL_driver_error(L, error);
        }
        if ((error = i2c_write(user_data->unit, &user_data->transaction, (char *)buf, count))) {
        	free(buf);
        	return luaL_driver_error(L, error);
        }
    	free(buf);
    	buf = NULL;
    }

	if (out_type < 2) luaL_buffinit(L, &b);
    else lua_newtable(L);

	// read data
    buf = malloc(size);
    if (buf == NULL) {
        return luaL_error(L, "error allocating receive buffer");
    }

    if ((error = i2c_start(user_data->unit, &user_data->transaction))) {
    	free(buf);
    	return luaL_driver_error(L, error);
    }
	if ((error = i2c_write_address(user_data->unit, &user_data->transaction, addr, 1))) {
    	free(buf);
    	return luaL_driver_error(L, error);
    }
    if ((error = i2c_read(user_data->unit, &user_data->transaction, (char *)buf, size))) {
    	free(buf);
    	return luaL_driver_error(L, error);
    }
    if ((error = i2c_stop(user_data->unit, &user_data->transaction))) {
    	free(buf);
    	return luaL_driver_error(L, error);
    }

	for (i = 0; i < size; i++) {
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

    free(buf);

    if (out_type < 2) luaL_pushresult(&b);

	return 1;
}

// Destructor
static int li2c_trans_gc (lua_State *L) {
	i2c_user_data_t *user_data = NULL;

    user_data = (i2c_user_data_t *)luaL_testudata(L, 1, "i2c.trans");
    if (user_data) {
    }

    return 0;
}

static const LUA_REG_TYPE li2c_map[] = {
    { LSTRKEY( "setup"   ),			LFUNCVAL( li2c_setup   ) },
	{ LSTRKEY( "MASTER"  ),			LINTVAL ( I2C_MASTER   ) },
	{ LSTRKEY( "SLAVE"   ),			LINTVAL ( I2C_SLAVE    ) },
	I2C_I2C0
	I2C_I2C1

	// Error definitions
	{LSTRKEY("error"     ),         LROVAL( i2c_error_map )},
    { LNILKEY, LNILVAL }
};

static const LUA_REG_TYPE li2c_trans_map[] = {
	{ LSTRKEY( "start"      ),		LFUNCVAL( li2c_start     ) },
    { LSTRKEY( "address"     ),		LFUNCVAL( li2c_address   ) },
    { LSTRKEY( "read"        ),		LFUNCVAL( li2c_read      ) },
    { LSTRKEY( "write"       ),		LFUNCVAL( li2c_write     ) },
    { LSTRKEY( "stop"        ),		LFUNCVAL( li2c_stop      ) },
    { LSTRKEY( "send"        ),		LFUNCVAL( li2c_send        ) },
    { LSTRKEY( "receive"     ),		LFUNCVAL( li2c_receive     ) },
    { LSTRKEY( "sendreceive" ),		LFUNCVAL( li2c_sendreceive ) },
    { LSTRKEY( "__metatable" ),  	LROVAL  ( li2c_trans_map ) },
	{ LSTRKEY( "__index"     ),   	LROVAL  ( li2c_trans_map ) },
	{ LSTRKEY( "__gc"        ),   	LFUNCVAL  ( li2c_trans_gc ) },
    { LNILKEY, LNILVAL }
};

LUALIB_API int luaopen_i2c( lua_State *L ) {
    luaL_newmetarotable(L,"i2c.trans", (void *)li2c_trans_map);
    return 0;
}

MODULE_REGISTER_MAPPED(I2C, i2c, li2c_map, luaopen_i2c);

#endif
