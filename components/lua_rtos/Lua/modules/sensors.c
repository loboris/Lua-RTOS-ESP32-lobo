/*
 * Lua RTOS, Sensors Lua module
 *
 * Copyright (C) 2015 - 2016
 * LoBo, IBEROXARXA SERVICIOS INTEGRALES, S.L. & CSS IBÉRICA, S.L.
 *
 * Author: LoBo (loboris@gmail.com)
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

#if LUA_USE_SENSORS

#include <string.h>
#include "lua.h"
#include "error.h"
#include "lauxlib.h"
#include "modules.h"

#if LUA_USE_BME280

#include "drivers/i2c.h"
#include "sensors/bme280.h"

#define I2C_MASTER_TX_BUF_DISABLE	0   	// I2C master do not need buffer
#define I2C_MASTER_RX_BUF_DISABLE	0   	// I2C master do not need buffer
#define WRITE_BIT		I2C_MASTER_WRITE	// I2C master write
#define READ_BIT		I2C_MASTER_READ		// I2C master read
#define ACK_CHECK_EN	0x1    				// I2C master will check ack from slave
#define ACK_CHECK_DIS	0x0    				// I2C master will not check ack from slave
#define ACK_VAL			0x0    				// I2C ack value
#define NACK_VAL		0x1					// I2C nack value

extern struct bme280_user_data_t *p_bme280;	//pointer to active BME280

static void msdelay(uint32_t msek) {
    vTaskDelay(msek / portTICK_RATE_MS);
}

static void print_driver_error(driver_error_t *error, int err) {
	printf(" DRIVER ERROR [%d]: type: %d, unit: %d, exc: %d\r\n", err, error->type, error->unit, error->exception);

    free(error);
}

// ================================ BME280 ===========================================

/*	\Brief          : The function is used as I2C bus write
 *	\Return         : Status of the I2C write
 *	\param dev_addr : The device address of the sensor
 *	\param reg_addr : Address of the register to which data is going to be written
 *	\param reg_data : Register data array, will be used for write the values into the register
 *	\param cnt      : The number of bytes to be written
 */

//---------------------------------------------------------------------
s8 BME280_I2C_bus_write(u8 dev_addr, u8 reg_addr, u8 *reg_data, u8 cnt)
{
	driver_error_t *error;

	/*printf("[bme280 wr] (%02x) %02x:", dev_addr, reg_addr);
	for (int i=0; i<cnt; i++) {
		printf(" %02x", reg_data[i]);
	}*/
    if ((error = i2c_start(p_bme280->unit, &p_bme280->transaction))) {
    	print_driver_error(error, -1);
    	return -1;
    }
	if ((error = i2c_write_address(p_bme280->unit, &p_bme280->transaction, dev_addr, 0))) {
    	print_driver_error(error, -2);
    	return -2;
    }
    if ((error = i2c_write(p_bme280->unit, &p_bme280->transaction, (char *)&reg_addr, 1))) {
    	print_driver_error(error, -3);
    	return -3;
    }
    if ((error = i2c_write(p_bme280->unit, &p_bme280->transaction, (char *)reg_data, cnt))) {
    	print_driver_error(error, -4);
    	return -4;
    }
    if ((error = i2c_stop(p_bme280->unit, &p_bme280->transaction))) {
    	print_driver_error(error, -5);
    	return -5;
    }

	//printf("\r\n");
	return 0;
}

 /*	\Brief          : The function is used as I2C bus write&read
 *	\Return         : Status of the I2C write&read
 *	\param dev_addr : The device address of the sensor
 *	\param reg_addr : Address of the first register, from where data is going to be read
 *	\param reg_data : Array of data read from the sensor
 *	\param cnt      : The number of data bytes to be read
 */
//--------------------------------------------------------------------
s8 BME280_I2C_bus_read(u8 dev_addr, u8 reg_addr, u8 *reg_data, u8 cnt)
{
	driver_error_t *error;

	//printf("[bme280 rd] (%02x) [%d] %02x:", dev_addr, cnt, reg_addr);
    if ((error = i2c_start(p_bme280->unit, &p_bme280->transaction))) {
    	print_driver_error(error, -1);
    	return -1;
    }
	if ((error = i2c_write_address(p_bme280->unit, &p_bme280->transaction, dev_addr, 0))) {
    	print_driver_error(error, -2);
    	return -2;
    }
    if ((error = i2c_write(p_bme280->unit, &p_bme280->transaction, (char *)&reg_addr, 1))) {
    	print_driver_error(error, -3);
    	return -3;
    }
	// read reg data
    if ((error = i2c_start(p_bme280->unit, &p_bme280->transaction))) {
    	print_driver_error(error, -5);
    	return -6;
    }
	if ((error = i2c_write_address(p_bme280->unit, &p_bme280->transaction, dev_addr, 1))) {
    	print_driver_error(error, -7);
    	return -7;
    }
	if ((error = i2c_read(p_bme280->unit, &p_bme280->transaction, (char *)reg_data, cnt))) {
    	print_driver_error(error, -8);
    	return -8;
    }
    if ((error = i2c_stop(p_bme280->unit, &p_bme280->transaction))) {
    	print_driver_error(error, -10);
    	return -10;
    }

	/*for (int i=0; i<cnt; i++) {
		printf(" %02x", reg_data[i]);
	}
	printf("\r\n");*/
    return 0;
}

/*	Brief : The delay routine
 *	\param : delay in ms
*/
//------------------------------
void BME280_delay_msek(u32 msek)
{
    vTaskDelay(msek / portTICK_RATE_MS);
}

//---------------------------------------------------------
static void bme280_getsby(lua_State* L, int index, u8 *sby)
{
	if (lua_gettop(L) >= index) {
		int sb = luaL_checkinteger(L, index);
		if (sb < 10) *sby = BME280_STANDBY_TIME_1_MS;
		else if (sb < 20) *sby = BME280_STANDBY_TIME_10_MS;
		else if (sb < 62) *sby = BME280_STANDBY_TIME_20_MS;
		else if (sb < 125) *sby = BME280_STANDBY_TIME_63_MS;
		else if (sb < 250) *sby = BME280_STANDBY_TIME_125_MS;
		else if (sb < 500) *sby = BME280_STANDBY_TIME_250_MS;
		else if (sb < 1000) *sby = BME280_STANDBY_TIME_500_MS;
		else *sby = BME280_STANDBY_TIME_1000_MS;
	}
}

extern char *i2c_errors[];

/*
 * Create BME280 instance on specified I2C channel
 * Returns bme instance
 * Parameters:
 * i2c_id: possible values i2c.I2C0 (0) & i2c.I2C1 (1)
 * i2cspeed: speed in kHz
 * sda: SDA pin
 * sdc: SDC pin
 * bme280_instance = bme280.setup(i2c_id, i2c_speed, sda, scl)
 * Notes:
 * - uses i2c driver for operation
 * - first read (get) can return wrong values
 */
//====================================
static int bme280_setup(lua_State* L)
{
	driver_error_t *error;

    int id = luaL_checkinteger(L, 1);
    int speed = luaL_checkinteger(L, 2);
    int sda = luaL_checkinteger(L, 3);
    int scl = luaL_checkinteger(L, 4);

    if ((error = i2c_setup(id, I2C_MASTER, speed, sda, scl, 0, 0))) {
    	return luaL_driver_error(L, error);
    }

    // Allocate userdata
    struct bme280_user_data_t *user_data;
    user_data = (struct bme280_user_data_t *)lua_newuserdata(L, sizeof(struct bme280_user_data_t));
    if (!user_data) {
       	return luaL_exception(L, I2C, I2C_ERR_NOT_ENOUGH_MEMORY, i2c_errors);
    }

    user_data->unit = id;
    user_data->transaction = I2C_TRANSACTION_INITIALIZER;

    luaL_getmetatable(L, "bme280");
    lua_setmetatable(L, -2);
    return 1;
}

/*
 * Initialize BME280 sensor
 * Optional parameters can be given:
 * addr: i2c address; default 0x76; possible values: 0x76 & 0x77
 * mode: BME280 operating mode; default: sleep mode; possible values 0,1 & 3
 *       0: sleep mode, no operation, lowest power
 *       1: forced mode, perform one measurement, return to sleep mode
 *       3: normal mode, continuous measurements with inactive periods between
 *  per: standby period between measurements in NORMAL MODE in msec; default: 125
 * Returns initialization status which is 0 in case of no error
 * res = bme280_instance:init([addr, mode, per])
 */
//====================================
static int bme280_doinit(lua_State* L)
{
	u8 sby = BME280_STANDBY_TIME_125_MS;
	u8 addr = BME280_I2C_ADDRESS1;
	u8 mode = BME280_SLEEP_MODE;
    s32 com_rslt = ERROR;

    // Get user data
    p_bme280 = (struct bme280_user_data_t *)luaL_checkudata(L, 1, "bme280");
    luaL_argcheck(L, p_bme280, 1, "i2c transaction expected");

	if (lua_gettop(L) > 1) {
		addr = luaL_checkinteger(L, 2);
		if ((addr != BME280_I2C_ADDRESS1) && (addr != BME280_I2C_ADDRESS2)) addr = BME280_I2C_ADDRESS1;
	}

	if (lua_gettop(L) > 2) {
		int md = luaL_checkinteger(L, 3);
		if ((md == BME280_NORMAL_MODE) || (md == BME280_FORCED_MODE)) mode = md;

		if ((mode == BME280_NORMAL_MODE) && (lua_gettop(L) > 3)) {
			bme280_getsby(L, 4, &sby);
		}
	}

	p_bme280->chip_id = 0;
	p_bme280->mode = 255;
	p_bme280->bus_write = BME280_I2C_bus_write;
	p_bme280->bus_read = BME280_I2C_bus_read;
	p_bme280->dev_addr = addr;
	p_bme280->delay_msec = BME280_delay_msek;

	/*--------------------------------------------------------------------------*
	 *  This function used to assign the value/reference of
	 *	the following parameters
	 *	I2C address
	 *	Bus Write
	 *	Bus read
	 *	Chip id
	*-------------------------------------------------------------------------*/
	com_rslt = bme280_init();
	if (com_rslt == BME280_CHIP_ID_READ_SUCCESS) {
		p_bme280->mode = mode;

		com_rslt += bme280_set_soft_rst();
	    msdelay(5);

		//	For reading the pressure, humidity and temperature data it is required to
		//	set the OSS setting of humidity, pressure and temperature
		// set the humidity oversampling
		com_rslt += bme280_set_oversamp_humidity(BME280_OVERSAMP_1X);
		// set the pressure oversampling
		com_rslt += bme280_set_oversamp_pressure(BME280_OVERSAMP_2X);
		// set the temperature oversampling
		com_rslt += bme280_set_oversamp_temperature(BME280_OVERSAMP_4X);
		// set standby time
		com_rslt += bme280_set_standby_durn(sby);

		com_rslt += bme280_set_power_mode(mode);
	}

	// **************** END INITIALIZATION ****************
	lua_pushinteger(L, com_rslt);
	return 1;
}

/*
 * Set BME280 operating mode
 * Returns operation result which is 0 if no error
 * Parameters:
 * mode: operating mode
 *       0: sleep mode, no operation, lowest power
 *       1: forced mode, perform one measurement, return to sleep mode
 *       3: normal mode, continuous measurements with inactive periods between
 *  per: optional; standby period between measurements in NORMAL MODE in msec; default: 125
 * res = bme280_instance:setmode(mode)
 */
//=====================================
static int bme280_setmode(lua_State* L)
{
	u8 mode, md;
	s32 com_rslt = ERROR;

    // Get user data
    p_bme280 = (struct bme280_user_data_t *)luaL_checkudata(L, 1, "bme280");
    luaL_argcheck(L, p_bme280, 1, "i2c transaction expected");

	if (p_bme280->chip_id == BME280_CHIP_ID) {
		md = luaL_checkinteger(L, 2);
		if ((md == BME280_SLEEP_MODE) || (md == BME280_FORCED_MODE) || (md == BME280_NORMAL_MODE)) {
			com_rslt = bme280_get_power_mode(&mode);
			if (com_rslt == 0) {
				if (mode != md) {
					com_rslt = bme280_set_power_mode(md);
					if (com_rslt == 0) {
						p_bme280->mode = md;
						if ((md == BME280_NORMAL_MODE) && (lua_gettop(L) > 2)) {
							u8 sby = 255;
							bme280_getsby(L, 3, &sby);
							if (sby < 8) com_rslt = bme280_set_standby_durn(sby);
						}
					}
				}
			}
		}
	}

	lua_pushinteger(L, com_rslt);
	return 1;
}

/*
 * Get current BME280 operating mode
 * Returns:
 * mode: current operating mode
 *     0: sleep mode, no operation, lowest power
 *     1: forced mode, perform one measurement, return to sleep mode
 *     3: normal mode, continuous measurements with inactive periods between
 *   < 0: error
 * sby: standby period if mode = 3
 *     msek period
 *     nil: not in normal mode or error
 *
 * mode, sby = bme280_instance:getmode()
 */
//=====================================
static int bme280_getmode(lua_State* L)
{
	u8 mode;
	u8 sby = 255;
	s32 com_rslt = ERROR;

    // Get user data
    p_bme280 = (struct bme280_user_data_t *)luaL_checkudata(L, 1, "bme280");
    luaL_argcheck(L, p_bme280, 1, "i2c transaction expected");

	if (p_bme280->chip_id == BME280_CHIP_ID) {
		com_rslt = bme280_get_power_mode(&mode);
		if (com_rslt == 0) {
			com_rslt += bme280_get_standby_durn(&sby);
			if (com_rslt != 0) sby = 255;
			com_rslt = mode;
		}
	}

	lua_pushinteger(L, com_rslt);
	if (sby < 255) {
		int sb = 1000;
		if (sby == BME280_STANDBY_TIME_1_MS) sb = 1;
		else if (sby == BME280_STANDBY_TIME_10_MS) sb = 10;
		else if (sby == BME280_STANDBY_TIME_20_MS) sb = 20;
		else if (sby == BME280_STANDBY_TIME_63_MS) sb = 63;
		else if (sby == BME280_STANDBY_TIME_125_MS) sb = 125;
		else if (sby == BME280_STANDBY_TIME_250_MS) sb = 250;
		else if (sby == BME280_STANDBY_TIME_500_MS) sb = 500;
		lua_pushinteger(L, sb);
	}
	else lua_pushnil(L);

	return 2;
}


/*
 * Get temperature, pressure and humidity from BME280 sensor
 * Returns float values, in case of error all three values equals 0.0
 * units: temperature: deg C; humidity: %; preasure: hPa
 * temp, hum, pres = bme280_instance:get()
 */
//=================================
static int bme280_get(lua_State* L)
{
    // Get user data
    p_bme280 = (struct bme280_user_data_t *)luaL_checkudata(L, 1, "bme280");
    luaL_argcheck(L, p_bme280, 1, "i2c transaction expected");

	// The variable used to read uncompensated temperature
	s32 v_data_uncomp_temp_s32 = BME280_INIT_VALUE;
	// The variable used to read uncompensated pressure
	s32 v_data_uncomp_pres_s32 = BME280_INIT_VALUE;
	// The variable used to read uncompensated humidity
	s32 v_data_uncomp_hum_s32 = BME280_INIT_VALUE;

	double temp = 0.0;
	double pres = 0.0;
	double hum = 0.0;
	s32 com_rslt = ERROR;

	if (p_bme280->chip_id == BME280_CHIP_ID) {
		if (p_bme280->mode != BME280_NORMAL_MODE)
			com_rslt = bme280_get_forced_uncomp_pressure_temperature_humidity(
					&v_data_uncomp_pres_s32, &v_data_uncomp_temp_s32, &v_data_uncomp_hum_s32);
		else com_rslt = bme280_read_uncomp_pressure_temperature_humidity(
				&v_data_uncomp_pres_s32, &v_data_uncomp_temp_s32, &v_data_uncomp_hum_s32);

		printf("BME280 READ uncomp %d, data: %d  %d  %d\r\n", com_rslt, v_data_uncomp_pres_s32, v_data_uncomp_temp_s32, v_data_uncomp_hum_s32);
		temp = bme280_compensate_temperature_double(v_data_uncomp_temp_s32);
		pres = bme280_compensate_pressure_double(v_data_uncomp_pres_s32);
		hum = bme280_compensate_humidity_double(v_data_uncomp_hum_s32);
	}

	lua_pushnumber(L, temp);
	lua_pushnumber(L, hum);
	lua_pushnumber(L, pres / 100.0);
	return 3;
}

//====================================================================================

// Destructor
static int lbme280_trans_gc (lua_State *L) {
	struct bme_user_data_t *user_data = NULL;

    user_data = luaL_testudata(L, 1, "bme280");
    if (user_data) {
    }

    return 0;
}

static int lbme280_index(lua_State *L);
static int lbme280_trans_index(lua_State *L);

static const LUA_REG_TYPE lbme280_error_map[] = {
};

static const LUA_REG_TYPE lbme280_map[] = {
    { LSTRKEY( "setup"   ),			LFUNCVAL( bme280_setup   ) },
    { LNILKEY, LNILVAL }
};

static const LUA_REG_TYPE lbme280_trans_map[] = {
	{ LSTRKEY( "init" ),	LFUNCVAL(bme280_doinit ) },
	{ LSTRKEY( "getmode" ),	LFUNCVAL(bme280_getmode ) },
	{ LSTRKEY( "setmode" ),	LFUNCVAL(bme280_setmode ) },
	{ LSTRKEY( "get" ),		LFUNCVAL(bme280_get ) },
    { LNILKEY, LNILVAL }
};

static const LUA_REG_TYPE lbme280_constants_map[] = {
	// Error definitions
	{LSTRKEY("error"     ),         LROVAL( lbme280_error_map )},

	{ LNILKEY, LNILVAL }
};

static const luaL_Reg lbme280_func[] = {
    { "__index", 	lbme280_index },
    { NULL, NULL }
};

static const luaL_Reg lbme280_trans_func[] = {
	{ "__gc"   , 	lbme280_trans_gc },
    { "__index", 	lbme280_trans_index },
    { NULL, NULL }
};

static int lbme280_index(lua_State *L) {
	return luaR_index(L, lbme280_map, lbme280_constants_map);
}

static int lbme280_trans_index(lua_State *L) {
	return luaR_index(L, lbme280_trans_map, NULL);
}


int luaopen_bme280(lua_State* L) {
    luaL_newlib(L, lbme280_func);
    lua_pushvalue(L, -1);
    lua_setmetatable(L, -2);

    luaL_newmetatable(L, "bme280");
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");

    luaL_setfuncs(L, lbme280_trans_func, 0);
    lua_pop(L, 1);

    return 1;
}

LIB_INIT(BME280, bme280, luaopen_bme280);

#endif

#if LUA_USE_OW
/*====================================================================================
* 1-wire and DS1820 functions
* ====================================================================================
*/
#include "drivers/owire.h"
#include "sensors/ds1820.h"

static unsigned char ow_numdev = 0;
static unsigned char ds1820_numdev = 0;
static unsigned char ow_roms[MAX_ONEWIRE_SENSORS][8];
uint32_t ds_measure_time;

//----------------------------------------------------
unsigned char check_dev(unsigned char n, int ds1820) {
  if (n == 0) return 0;
  if ((ds1820) && ((ds1820_numdev == 0) || (n > ds1820_numdev))) return 0;
  else if ((ow_numdev == 0) || (n > ow_numdev)) return 0;
  return 1;
}

//-------------------------------------------
static void _set_measure_time(int resolution)
{
  switch (resolution) {
	case 9:
	  ds_measure_time = 100;
	  break;
	case 10:
	  ds_measure_time = 200;
	  break;
	case 11:
	  ds_measure_time = 400;
	  break;
	case 12:
	  ds_measure_time = 800;
	  break;
	default:
	  ds_measure_time = 800;
  }
}

/* Get current DS1820 resolution of the selected device
 *
 */
//=============================================
static int lsensor_18b20_getres( lua_State* L )
{
  unsigned char dev = 0;
  unsigned char res = 0;

  dev = luaL_checkinteger( L, 1 );
  if ((!check_dev(dev,1) || (!TM_DS18B20_Is(&(ow_roms[dev-1][0]))))) {
     lua_pushnil(L);
     return 1;
  }

  // Get resolution
  res = TM_DS18B20_GetResolution(ow_roms[dev-1]);
  _set_measure_time(res);

  lua_pushinteger(L, res);
  lua_pushinteger(L, ds_measure_time);

  return 2;
}

/* Set DS1820 resolution of the selected device
 * 9, 10, 11, 12 bits resolution can be used
 */
//=============================================
static int lsensor_18b20_setres( lua_State* L )
{
  unsigned char dev = 0;
  unsigned char res = 0;
  owState_t stat;

  dev = luaL_checkinteger( L, 1 );
  res = luaL_checkinteger( L, 2 );

  if ((!check_dev(dev,1) || (!TM_DS18B20_Is(&(ow_roms[dev-1][0]))))) {
     lua_pushnil(L);
     return 1;
  }

  if ( res!=TM_DS18B20_Resolution_9bits &&
       res!=TM_DS18B20_Resolution_10bits &&
       res!=TM_DS18B20_Resolution_11bits &&
       res!=TM_DS18B20_Resolution_12bits ) {
    res = TM_DS18B20_Resolution_12bits;
  }
  // Set resolution
  if (ow_roms[dev-1][0] == DS18S20_FAMILY_CODE) {
    res = TM_DS18B20_Resolution_12bits;
    stat = ow_OK;
  }
  else {
	stat = TM_DS18B20_SetResolution(ow_roms[dev-1], (TM_DS18B20_Resolution_t)res);
  }

  if (stat != ow_OK) lua_pushinteger(L, stat);
  else {
	  res = TM_DS18B20_GetResolution(ow_roms[dev-1]);
	  lua_pushinteger(L, res);
  }
  _set_measure_time(res);

  return 1;
}

/* Start temperature measurement and wait for result
 *
 */
//==============================================
static int lsensor_18b20_gettemp( lua_State* L )
{
  unsigned char dev = 0;
  owState_t stat;
  double temper;

  dev = luaL_checkinteger( L, 1 );
  if ((!check_dev(dev,1) || (!TM_DS18B20_Is(&(ow_roms[dev-1][0]))))) {
    lua_pushinteger(L, -9999);
    lua_pushinteger(L, -1);
    return 2;
  }

  int res = TM_DS18B20_GetResolution(ow_roms[dev-1]);
  _set_measure_time(res);

  // Start temperature conversion on all devices on one bus
  TM_DS18B20_StartAll();

  // Wait until all are done on one onewire port (max 1.5 second)
  unsigned int tmo;
  if (ds_parasite_pwr) {
	  tmo = 0;
	  while (tmo < ds_measure_time) {
		msdelay(10);
		tmo += 10;
	  }
	  owdevice_input();
	  ds_start_measure_time = 0;
 	  msdelay(10);
  }
  else {
	  tmo = 0;
	  while (tmo < ds_measure_time) {
		msdelay(10);
		if (TM_OneWire_ReadBit()) break;
		tmo += 10;
	  }
	  if (tmo >= ds_measure_time) {
		/* Timeout */
		lua_pushinteger(L, -9999);
		lua_pushinteger(L, owError_NotReady);
		return 2;
	  }
  }

  // Read temperature from selected device
  // Read temperature from ROM address and store it to temper variable
  stat = TM_DS18B20_Read(ow_roms[dev-1], &temper);
  if ( stat == ow_OK) {
    lua_pushnumber(L, temper);
    lua_pushinteger(L, stat);
  }
  else {
    /* Reading error */
    lua_pushinteger(L, -9999);
    lua_pushinteger(L, stat);
  }

  return 2;
}

/* Start temperature measurement on all devices
 *
 */
//=============================================
static int lsensor_18b20_startm( lua_State* L )
{
  if (ds1820_numdev == 0) {
	// no DS1820 devices
    lua_pushinteger(L, -1);
    return 1;
  }

  // Start temperature conversion on all devices on one bus
  TM_DS18B20_StartAll();

  lua_pushinteger(L, 0);
  return 1;
}

/* Get the last measured value from device
 * Measurement must be initialed using ow.ds1820.startm() function
 */
//==========================================
static int lsensor_18b20_get( lua_State* L )
{
  unsigned char dev = 0;

  dev = luaL_checkinteger( L, 1 );
  if ((!check_dev(dev,1) || (!TM_DS18B20_Is(&(ow_roms[dev-1][0]))))) {
    lua_pushinteger(L, -9999);
	lua_pushinteger(L, owError_Not18b20);
	return 2;
  }

  // Check if measurement finished
  if (ds_parasite_pwr) {
 	  struct timeval now;
	  gettimeofday(&now, NULL);
	  if ((now.tv_usec - ds_start_measure_time) < (ds_measure_time * 1000)) {
		lua_pushinteger(L, -9999);
		lua_pushinteger(L, owError_NotReady);
		return 2;
	  }
	  owdevice_input();
	  ds_start_measure_time = 0;
	  msdelay(10);
  }
  else {
	  if (TM_OneWire_ReadBit() == 0) {
		lua_pushinteger(L, -9999);
		lua_pushinteger(L, owError_NotReady);
		return 2;
	  }
  }

  owState_t stat;
  double temper;

  // Read temperature from selected device
  // Read temperature from ROM address and store it to temper variable
  stat = TM_DS18B20_Read(ow_roms[dev-1], &temper);

  if ( stat == ow_OK) lua_pushnumber(L, temper);
  else lua_pushinteger(L, -9999); // error
  lua_pushinteger(L, stat);

  return 2;
}

/* Search for 1-wire devices on 1-wire bus
 * Returns number of found devices
 * num = ow.search()
 */
//==========================================
static int lsensor_ow_search( lua_State* L )
{
  unsigned char count = 0;
  unsigned char ds1820_count = 0;
  unsigned char owdev = 0;

  owdev = TM_OneWire_First();
  while (owdev) {
    count++;  // Increase device counter

    // Get full ROM value, 8 bytes, give location of first byte where to save
    TM_OneWire_GetFullROM(ow_roms[count - 1]);
    // Test if it is DS1820 device
    if (TM_DS18B20_Is(&(ow_roms[count - 1][0]))) {
    	ds1820_count++;
    }
    // Get next device
    owdev = TM_OneWire_Next();
    if (count >= MAX_ONEWIRE_SENSORS) break;
  }

  ow_numdev = count;
  ds1820_numdev = ds1820_count;
  lua_pushinteger(L, ow_numdev);
  return 1;
}

/* Initialize 1-wire bus on given gpio
 * Returns 1 if any device is detected on the bus,
 * 0 if no device is detected
 * res = ow.init(gpio)
 */
//========================================
static int lsensor_ow_init( lua_State* L )
{
  driver_error_t *error;
  unsigned pin=0;

  pin = luaL_checkinteger( L, 1 );
  if ((error = owire_setup_pin(pin))) {
  	return luaL_driver_error(L, error);
  }

  OW_DEVICE.pin = pin;
  if (owdevice_input() != 0) {
      return luaL_error(L, "error configuring pin %d", pin);
  }

  msdelay(10);

  ow_numdev = 0;

  // Check if any device connected
  if (TM_OneWire_Reset() == 0) lua_pushinteger(L, 1);
  else lua_pushinteger(L, 0);
  return 1;
}

/* Get device ROM of the specified device (8 bytes)
 * to Lua table or string
 * rom_table = ow.getrom(id)
 * rom_string = ow.getrom(id, "*h")
 * Returns empty table or string if wrong device id is given
 */
//==========================================
static int lsensor_ow_getrom( lua_State* L )
{
  unsigned dev = 0;
  int i;
  luaL_Buffer b;
  char hbuf[4];

  dev = luaL_checkinteger( L, 1 );

  int hexout = 0;
  if (lua_isstring(L, 2)) {
      const char* sarg;
      size_t sarglen;
      sarg = luaL_checklstring(L, 2, &sarglen);
      if (sarglen == 2) {
      	if (strstr(sarg, "*h") != NULL) hexout = 1;
      }
  }
  if (hexout) luaL_buffinit(L, &b);
  else lua_newtable(L);

  if (check_dev(dev, 0)) {
	  for (i = 0; i < 8; i++) {
		  if (hexout) {
			sprintf(hbuf, "%02x;", ow_roms[dev-1][i]);
			luaL_addstring(&b, hbuf);
		  }
		  else {
			  lua_pushinteger( L, ow_roms[dev-1][i] );
			  lua_rawseti(L,-2,i + 1);
		  }
	  }
  }
  if (hexout) luaL_pushresult(&b);
  return 1;
}

/* list devices detected on ow bus
 * device ROM and type (if known) are printed
 */
//==========================================
static int lsensor_ow_listdev( lua_State* L )
{
  unsigned char code;
  int i,j;
  char family[16];

  for (i=0;i<ow_numdev;i++) {
	  printf("%02d [", i+1);
	  for (j = 0; j < 8; j++) {
			printf("%02x", ow_roms[i][j]);
			if (j < 7) printf(" ");
	  }
	  printf("] ");
	  code = TM_DS18B20_Is(&(ow_roms[i][0]));
	  if (code) {
		  TM_DS18B20_Family(code, family);
		  printf("%s\r\n", family);
	  }
	  else printf("unknown\r\n");
  }
  return 0;
}

//====================================================================================

static const LUA_REG_TYPE lds1820_map[] = {
	{ LSTRKEY( "gettemp" ),		LFUNCVAL(lsensor_18b20_gettemp ) },
	{ LSTRKEY( "get" ),			LFUNCVAL(lsensor_18b20_get ) },
	{ LSTRKEY( "startm" ),		LFUNCVAL(lsensor_18b20_startm ) },
	{ LSTRKEY( "getres" ),		LFUNCVAL(lsensor_18b20_getres ) },
	{ LSTRKEY( "setres" ),		LFUNCVAL(lsensor_18b20_setres ) },
#if LUA_USE_ROTABLE
#endif
    { LNILKEY, LNILVAL }
};

static const LUA_REG_TYPE low_map[] = {
	{ LSTRKEY( "init"   ),	LFUNCVAL(lsensor_ow_init ) },
	{ LSTRKEY( "search" ),	LFUNCVAL(lsensor_ow_search ) },
	{ LSTRKEY( "getrom" ),	LFUNCVAL(lsensor_ow_getrom ) },
	{ LSTRKEY( "list"   ),	LFUNCVAL(lsensor_ow_listdev ) },
	{ LSTRKEY( "ds1820" ),	LROVAL  ( lds1820_map ) },
#if LUA_USE_ROTABLE
#endif
    { LNILKEY, LNILVAL }
};

LUALIB_API int luaopen_ow(lua_State *L) {
	return 0;
}

LUA_OS_MODULE(OW, ow, low_map);

#endif

#endif
