
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>
#include <string.h>
//#include "freertos/FreeRTOS.h"
//#include "freertos/task.h"
//#include "esp_system.h"
#include "drivers/arducam.h"
#include "time.h"
//#include <math.h>

#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
#include "auxmods.h"
#include "error.h"

#if CONFIG_LUA_RTOS_LUA_USE_CAM

#define OV2640_CHIPID_HIGH  0x0A
#define OV2640_CHIPID_LOW   0x0B
#define READ_BUF_LEN		1002

//====================================
static int lcam_init( lua_State* L ) {
    uint8_t vid, pid, temp;

    uint8_t sda = luaL_checkinteger( L, 1);
    uint8_t scl = luaL_checkinteger( L, 2);
    uint8_t sdi = luaL_checkinteger( L, 3);
    uint8_t sdo = luaL_checkinteger( L, 4);
    uint8_t sck = luaL_checkinteger( L, 5);
    uint8_t cs = luaL_checkinteger( L, 6);


    int res = arducam(smOV2640, sda, scl, sdi, sdo, sck, cs);
    if (res != 0) {
    	lua_pushboolean(L, false);
    	return 1;
    }
    // Check if the ArduCAM SPI bus is OK
    arducam_write_reg(ARDUCHIP_TEST1, 0x55);
    temp = arducam_read_reg(ARDUCHIP_TEST1);
    if(temp != 0x55) {
    	lua_pushboolean(L, false);
    	return 1;
    }

    // Check if the camera module type is OV2640
    arducam_i2c_read(OV2640_CHIPID_HIGH, &vid);
    arducam_i2c_read(OV2640_CHIPID_LOW, &pid);
    if((vid != 0x26) || (pid != 0x42)) {
    	lua_pushboolean(L, false);
    	return 1;
    }

    // set default format and size
    arducam_set_format(fmtJPEG);
    res = arducam_init();
    if (res != 0) {
    	lua_pushboolean(L, false);
    	return 1;
    }
    arducam_set_jpeg_size(sz640x480);
    // Allow for auto exposure loop to adapt to after resolution change
	arducam_delay_ms(100);

	cam_initialized = 1;
	lua_pushboolean(L, true);
	return 1;
}

//---------------------------------
static uint32_t cam_capture(void) {
    if (!cam_initialized) {
        return 0;
    }
    //arducam_flush_fifo();		// TODO: These are the same
    arducam_clear_fifo_flag();	// TODO: These are the same

    arducam_start_capture();
    uint8_t temp = arducam_read_reg(ARDUCHIP_TRIG);
    if (!temp) {
        return 0;
    }
    int tmo = 0;
    while (!(arducam_read_reg(ARDUCHIP_TRIG) & CAP_DONE_MASK)) {
       	arducam_delay_ms(2);
       	tmo++;
       	if (tmo > 1000) return 0;
    }
    uint32_t fifo_size = (arducam_read_reg(0x44) << 16) | (arducam_read_reg(0x43) << 8) | (arducam_read_reg(0x42));
    return fifo_size;
}

//=======================================
static int lcam_capture( lua_State* L ) {
    uint32_t start_time = clock();

    uint32_t fifo_size = cam_capture();

    lua_pushinteger(L, fifo_size);
    lua_pushinteger(L, (uint32_t)(clock()-start_time));

    return 2;
}

//------------------------------------------------------------------------------------
uint8_t *cam_get_image(FILE *fhndl, int *err, uint32_t *bytes_read, uint8_t capture) {
    if (!cam_initialized) {
		*err = -8;
		return NULL;
    }

    uint32_t fifo_size;
    if (capture) fifo_size = cam_capture();
    else fifo_size = (arducam_read_reg(0x44) << 16) | (arducam_read_reg(0x43) << 8) | (arducam_read_reg(0x42));

    if ((fifo_size < 256) || (fifo_size > (384*1024))) {
		*err = -10;
		return NULL;
	}

	if ((!fhndl) && (fifo_size > (64*1024))) {
		*err = -1;
		return NULL;
	}

    uint8_t temp=0, temp_last=0, endID=0;;
	int buf_idx;
	*bytes_read = 0;
    *err = 0;
	uint8_t *outbuf = NULL;
	if (!fhndl) {
		// Get image to buffer
		outbuf = malloc(fifo_size+8);
		if (!outbuf) {
			*err = -2;
			return NULL;
		}
		memset(outbuf, 0, fifo_size+8);
    	arducam_burst_read_fifo(outbuf, fifo_size, 1);
    	arducam_burst_read_fifo(NULL, 0, 10); // end transfer

    	// Check for jpeg image start ID
		if ((outbuf[2] != 0xFF) || (outbuf[3] != 0xD8)) {
	    	free(outbuf);
			*err = -4;
	    	return NULL;
		}
		// Check for JPEG end ID
	    temp_last = outbuf[2];
	    temp = outbuf[3];
	    buf_idx = 4;
    	while (buf_idx < fifo_size) {
		    temp_last = temp;
		    temp = outbuf[buf_idx++];
		    if (buf_idx == fifo_size) break;
	        if ((temp == 0xD9) && (temp_last == 0xFF)) {
	        	endID = 1;
	        	break;
	        }
    	}
        if (!endID) {
	    	free(outbuf);
        	*err = -9;
	    	return NULL;
        }
        *bytes_read = buf_idx-2;
        return outbuf;
	}

	// Get image to file
	uint8_t *buf = malloc(READ_BUF_LEN);
	if (!buf) {
		*err = -3;
		return NULL;
	}

	int len;
	int buf_start;
    *err = 0;
	*bytes_read = 0;

	// Read JPEG data from FIFO
    while (*bytes_read < fifo_size) {
    	memset(buf, 0, READ_BUF_LEN);

    	if (*bytes_read == 0) {
    		if (fifo_size < (READ_BUF_LEN-2)) len = fifo_size+2;
    		else len = READ_BUF_LEN;

    		arducam_burst_read_fifo(buf, len, 1);
			// Check for JPEG start ID on first read
			if ((buf[2] != 0xFF) || (buf[3] != 0xD8)) {
				*err = -4;
				break;
			}
		    temp_last = buf[2];
		    temp = buf[3];
		    buf_idx = 4;
		    buf_start = 2;
    	}
    	else {
    		len = fifo_size - *bytes_read;	// remaining bytes to read
    		if (len == 0) break;			// all bytes read
    		if (len < 0) {					// something is wrong
    			*err = -7;
    			break;
    		}
    		if (len > READ_BUF_LEN) len = READ_BUF_LEN;

    		arducam_burst_read_fifo(buf, len, 0);
        	buf_idx = 0;
		    buf_start = 0;
    	}

    	while (1) {
    		// scan buffer, check for jpeg end ID (0xFFD9)
		    temp_last = temp;
		    temp = buf[buf_idx++];
		    if (buf_idx == len) break;
	        if ((temp == 0xD9) && (temp_last == 0xFF)) {
	        	endID = 1;
	        	break;
	        }
    	}

    	// Append content of the buffer to file
		if (fwrite(buf+buf_start, 1, buf_idx-buf_start, fhndl) != (buf_idx-buf_start)) {
			*err = -6;
			break;
		}
        *bytes_read += buf_idx-buf_start;
    	//printf("Read %d bytes, total read %u\r\n", len, *bytes_read);
        if (endID) break;

    }
	arducam_burst_read_fifo(NULL, 0, 10); // end transfer

    if (!endID) *err = -9;

	free(buf);
    if (*err) {
    	*bytes_read = 0;
    	return NULL;
    }

    return outbuf;
}

// cam.read(file_name [,capture]
//====================================
static int lcam_read( lua_State* L ) {
    uint32_t start_time = clock();
    const char *fname;
    FILE *fhndl = 0;
    uint8_t capt = 0;
    size_t fnlen;
    fname = luaL_checklstring( L, 1, &fnlen );

    if (lua_gettop(L) > 1) {
    	if (luaL_checkinteger(L, 2) != 0) capt = 1;
    }

    fhndl = fopen(fname, "w");
    if (!fhndl) {
        //return luaL_error(L, strerror(errno));
		lua_pushinteger(L, -19);
	    lua_pushinteger(L, (uint32_t)(clock()-start_time));
		return 1;
    }

    int err = 0;
	uint32_t bytes_read = 0;
    cam_get_image(fhndl, &err, &bytes_read, capt);

    fclose(fhndl);

    lua_pushinteger(L, err);
    lua_pushinteger(L, (uint32_t)(clock()-start_time));

    return 2;
}

//=======================================
static int lcam_setsize( lua_State* L ) {
    int size = luaL_checkinteger( L, 1 );
    if ((size < 0) || (size > 8)) {
        return luaL_error(L, "size must be 0~8, or use constants:\r\nSIZE176x120, SIZE176x144, SIZE320x240\r\n\SIZE352x288, SIZE640x480, SIZE800x600\r\nSIZE1024x768, SIZE1280x1024, SIZE1600x1200");
    }

    int err = arducam_set_jpeg_size(size);
    // Allow for auto exposure loop to adapt to after resolution change
	arducam_delay_ms(100);

	if (err) lua_pushboolean(L, false);
	else lua_pushboolean(L, true);

	return 1;
}


#include "modules.h"

static const LUA_REG_TYPE cam_map[] = {
    { LSTRKEY( "init"    ),			LFUNCVAL( lcam_init    ) },
    { LSTRKEY( "capture" ),			LFUNCVAL( lcam_capture ) },
    { LSTRKEY( "read"    ),			LFUNCVAL( lcam_read ) },
    { LSTRKEY( "setsize" ),			LFUNCVAL( lcam_setsize ) },
#if LUA_USE_ROTABLE
	// Constant definitions
	{ LSTRKEY( "ARDUCAM_MINI"  ),	LNUMVAL( smOV2640    ) },
	{ LSTRKEY( "SIZE176x120"   ),	LNUMVAL( sz160x120   ) },
	{ LSTRKEY( "SIZE176x144"   ),	LNUMVAL( sz176x144   ) },
	{ LSTRKEY( "SIZE320x240"   ),	LNUMVAL( sz320x240   ) },
	{ LSTRKEY( "SIZE352x288"   ),	LNUMVAL( sz352x288   ) },
	{ LSTRKEY( "SIZE640x480"   ),	LNUMVAL( sz640x480   ) },
	{ LSTRKEY( "SIZE800x600"   ),	LNUMVAL( sz800x600   ) },
	{ LSTRKEY( "SIZE1024x768"  ),	LNUMVAL( sz1024x768  ) },
	{ LSTRKEY( "SIZE1280x1024" ),	LNUMVAL( sz1280x1024 ) },
	{ LSTRKEY( "SIZE1600x1200" ),	LNUMVAL( sz1600x1200 ) },
#endif
    { LNILKEY, LNILVAL }
};

int luaopen_cam(lua_State* L) {
#if !LUA_USE_ROTABLE
    luaL_newlib(L, screen_map);

    return 1;
#else

    return 0;
#endif
}

MODULE_REGISTER_MAPPED(CAM, cam, cam_map, luaopen_cam);

#endif
