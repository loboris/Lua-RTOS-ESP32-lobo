/*
 * Lua RTOS, lua http module
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

#if LUA_USE_HTTP

#include "net.h"

#include "lwip/err.h"

#include <drivers/net.h>
#include "http/httpclient.h"
#include "esp_system.h"

extern void http_start();
extern void http_stop();

static int lhttp_start(lua_State* L) {
	driver_error_t *error;

	if ((error = net_check_connectivity())) {
    	return luaL_driver_error(L, error);
	}

	http_start();

	return 0;
}

static int lhttp_stop(lua_State* L) {
	driver_error_t *error;

	if ((error = net_check_connectivity())) {
    	return luaL_driver_error(L, error);
	}

	http_stop();

	return 0;
}

/*
 * Build POST request based on provided data in string or Lua table
 *   last part of the header is placed in 'hdr'
 *   body (payload) is placed in 'post_data'
 *   if sending the file, filename is placed in body in spetial format
 *   to be sent ba driver
 */
//--------------------------------------------------------------------------------------------------
static int _preparePost(lua_State* L, int pos, char **post_data, char **hdr, char *errmsg, int type)
{
	if ((!lua_istable(L, pos)) && (!lua_isstring(L, pos))) {
		sprintf(errmsg,"POST data parameter missing");
		return -1;
	}
	if ((lua_istable(L, pos)) && (type == 2)) {
		sprintf(errmsg,"STRING parameter expected");
		return -1;
	}

	int data_len = 0;
	int buf_len = 512;
	char content[256];
	char *tmp_data = NULL;

	// build boundary
	if (http_boundary != NULL) free(http_boundary);
	http_boundary = malloc(128);
	if ( http_boundary == NULL) {
		sprintf(errmsg,"POST memory (boundary) allocation error");
		return -2;
	}
	uint32_t randn = esp_random();
	randn = esp_random();
	while (randn < 1000) {
		randn = esp_random();
	}
	sprintf(http_boundary, "______%u______", randn);
	int bndry_len = strlen(http_boundary);

	if (lua_istable(L, pos)) {
		// === We have table parameter ===
		int new_len = 0;
		*post_data = malloc(512);
		if (*post_data == NULL) {
			sprintf(errmsg,"POST memory allocation error");
			return -2;
		}
		char content[256];

		// table is in the stack at index 'pos', scan it's content
		lua_pushnil(L);  // first key
		while (lua_next(L, pos) != 0) {
			// uses 'key' (at index -2) and 'value' (at index -1)
			if ((lua_isstring(L, -1)) && (lua_isstring(L, -2))) {
				// only use key-value pairs that are or can be converted to string
				size_t klen = 0;
				size_t vlen = 0;
				const char* key = lua_tolstring(L, -2, &klen);
				const char* value = lua_tolstring(L, -1, &vlen);

				if ((*post_data != NULL) && (klen > 0) && (vlen > 0)) {
					// build multipart entry in body for this parameter
					sprintf(content, "--%s\r\nContent-Disposition: form-data; name=\"%s\"\r\n", http_boundary, key);
					// test if body buffer needs expansion
					new_len = data_len + strlen(content) + vlen + bndry_len + 256;
					if (new_len > buf_len) {
						// expand more than needed to make some place for next parameter
						tmp_data = realloc(*post_data, new_len);
						if (tmp_data != NULL) {
							*post_data = tmp_data;
							buf_len = new_len;
						}
					}
					else tmp_data = *post_data;

					if (tmp_data != NULL) {
						sprintf((*post_data)+data_len, "%s", content);
						data_len += strlen(content);
						sprintf((*post_data)+data_len, "\r\n%s\r\n", value);
						data_len += (vlen + 4);
					}
				}
			}
			// removes 'value'; keeps 'key' for next iteration
			lua_pop(L, 1);
		}

		if (*post_data != NULL) {
			// add trailing dashes to body
			sprintf((*post_data)+data_len, "\r\n--%s--\r\n", http_boundary);
			data_len += (bndry_len + 8);
			// Generate header last line
			sprintf(content, "Content-Type: multipart/form-data; boundary=%s\r\nContent-Length: %d\r\n\r\n", http_boundary, data_len);
			*hdr = malloc(strlen(content)+1);
			if (*hdr == NULL) {
				free(*post_data);
				*post_data = NULL;
				sprintf(errmsg,"POST (hdr) memory allocation error");
				return -3;
			}
			sprintf(*hdr, "%s", content);
		}
	}
	else {
		// === We have string parameter ===
		size_t param_len = 0;
		const char *param_val = NULL;
		param_val = luaL_checklstring( L, pos, &param_len );

		if ((param_len < 3) || (param_len > 4096) || (param_val == NULL)) {
			sprintf(errmsg,"Error preparing POST data (json jength)");
			return -4;
		}
		*post_data = malloc(param_len+256);
		if (*post_data == NULL) {
			sprintf(errmsg,"POST memory allocation error");
			return -5;
		}
		switch (type) {
			case 1: // json string
				// Build header last line
				sprintf(content, "Content-Type: application/json\r\nContent-Length: %d\r\n\r\n", param_len);
				// Put value to body
				sprintf(*post_data, "%s", param_val);
				data_len = param_len;

				*hdr = malloc(strlen(content)+1);
				if (*hdr == NULL) {
					free(*post_data);
					*post_data = NULL;
					sprintf(errmsg,"POST (json hdr) memory allocation error");
					return -6;
				}
				sprintf(*hdr, "%s", content);
				break;
			case 2: { // file
				*hdr = malloc(256);
				if (*hdr == NULL) {
					free(*post_data);
					*post_data = NULL;
					sprintf(errmsg,"POST (file hdr) memory allocation error");
					return -6;
				}

				// check if file exists and it's length is greater than zero
				struct stat sb;
				if ((stat(param_val, &sb) == 0) && (sb.st_size > 0)) {
					// simulate multipart body entry to determine the content length
					sprintf(*post_data, "--%s\r\nContent-Disposition: form-data; name=filedata; filename=\"%s\"\r\nContent-Type: application/octet-stream\r\n\r\n", http_boundary, param_val);
					data_len += (strlen(*post_data) + sb.st_size + bndry_len + 8);

					sprintf(*hdr, "Content-Type: multipart/form-data; boundary=%s\r\nContent-Length: %d\r\n\r\n", http_boundary, data_len);
					strcat(*hdr, *post_data);
					// put file name to body to be used by driver
					sprintf(*post_data, "%s%s", HTTP_FILE_ID, param_val);
				}
				break;
			}
			default: // text string
				sprintf(content, "Content-Type: text/html; charset=utf-8\r\nContent-Length: %d\r\n", param_len);
		}
	}

	if (*post_data == NULL) {
		sprintf(errmsg,"Error preparing POST data");
		data_len = 0;
		if (*hdr != NULL) {
			free(*hdr);
			*hdr = NULL;
		}
		if ( http_boundary != NULL) {
			free(http_boundary);
			http_boundary = NULL;
		}
		return -7;
	}
	if (data_len == 0) {
		if (*post_data != NULL) {
			free(*post_data);
			*post_data = NULL;
		}
		if (*hdr != NULL) {
			free(*hdr);
			*hdr = NULL;
		}
		if ( http_boundary != NULL) {
			free(http_boundary);
			http_boundary = NULL;
		}
		sprintf(errmsg,"Error preparing POST data");
		return -8;
	}

	return data_len;
}


//==========================================================
static int _lnet_http_req(lua_State* L, int req, int type) {
	char getinfo[256];
	const char* url = luaL_checkstring( L, 1 );
	char *filename = NULL;
	int npar = 2;
	char *post_data = NULL;
	char *hdr = NULL;

	if (req == HTTP_REQ_GET) npar = 2;
	else if (req == HTTP_REQ_POST) npar = 3;

	// Check for filename
	if (lua_isstring(L, npar)) {
		filename = (char *)luaL_checkstring( L, npar );
	}

	luaL_Buffer b;
	luaL_Buffer hb;
	luaL_buffinit(L, &b);
	luaL_buffinit(L, &hb);
	http_response_t* p_resp = NULL;
	int postdata_len = 0;

	if (req == HTTP_REQ_POST) {
		postdata_len = _preparePost(L, 2, &post_data, &hdr, getinfo, type);
		if ( postdata_len <= 0) {
			if (post_data != NULL) free(post_data);
			if (hdr != NULL) free(hdr);
			if (http_boundary != NULL) free(http_boundary);
			lua_pushboolean(L, false);
			luaL_addstring(&b, getinfo);
			luaL_pushresult(&b);
			luaL_pushresult(&hb);
			return 3;
		}
		#if HTTP_DEBUG
		printf("PREPARED\r\nHEADER:\r\n[%s]\r\nBODY:\r\n[%s]\r\n", hdr, post_data);
		#endif
	}

	if (req == HTTP_REQ_GET) p_resp = http_request((char *)url, req, NULL);
	else if (req == HTTP_REQ_POST) p_resp = http_request_w_body((char *)url, req, hdr, post_data);

	if (post_data != NULL) free(post_data);
	if (hdr != NULL) free(hdr);
	if (http_boundary != NULL) free(http_boundary);

	if (p_resp != NULL) {
		if (p_resp->status == HTTP_SUCCESS)	{
			/* received file. If it is an image, store it.*/
			if (p_resp->p_header->content_type != NULL && strstr(p_resp->p_header->content_type, "image") != NULL) {
				// store as local file
				if (filename) {
					FILE* output = fopen(filename, "wb");
					if (output != NULL) {
						fwrite(p_resp->contents, 1, p_resp->length, output);
						fclose(output);
					}
					sprintf(getinfo, "Received image fo file");
					lua_pushboolean(L, true);
				}
				else {
					sprintf(getinfo, "Received image, not saved");
					lua_pushboolean(L, false);
				}
				luaL_addstring(&b, getinfo);
			}
			else {
				// not an image
				if (filename) {
					FILE* output = fopen(filename, "wb");
					if (output != NULL) {
						fwrite(p_resp->contents, 1, p_resp->length, output);
						fclose(output);
						sprintf(getinfo, "Received to file, len=%d", p_resp->length);
					}
					else {
						sprintf(getinfo, "Receive to file, error opening file");
					}
					luaL_addstring(&b, getinfo);
				}
				else {
					luaL_addlstring(&b, p_resp->contents, p_resp->length);
				}
				lua_pushboolean(L, true);
			}
			luaL_addstring(&hb, p_resp->p_header->content);
		}
		else if (p_resp->status == HTTP_EMPTY_BODY) {
			snprintf(getinfo, 256, "no content: Reason: %s (%d)", p_resp->p_header->status_text, p_resp->p_header->status_code);
			luaL_addstring(&b, getinfo);
			lua_pushboolean(L, true);
			luaL_addstring(&hb, p_resp->p_header->content);
		}
		else {
			#if HTTP_DEBUG
			printf("HTTP_REQUEST ERROR\r\n");
			#endif
			print_status(p_resp->status, getinfo);
			luaL_addstring(&b, getinfo);
			lua_pushboolean(L, false);
		}
		http_response_free(p_resp);
		p_resp->p_header = NULL;
		p_resp = NULL;
	}
	else {
		lua_pushboolean(L, false);
		luaL_addstring(&b, "ERROR processing request");
	}

	luaL_pushresult(&b);
	luaL_pushresult(&hb);
	return 3;
}

//=================================
static int lnet_get(lua_State* L) {
	return _lnet_http_req(L, HTTP_REQ_GET, 0);
}

//==================================
static int lnet_post(lua_State* L) {
	return _lnet_http_req(L, HTTP_REQ_POST, 1);
}

//======================================
static int lnet_postfile(lua_State* L) {
	return _lnet_http_req(L, HTTP_REQ_POST, 2);
}

static const LUA_REG_TYPE http_map[] = {
    { LSTRKEY( "serverstart" ),	 LFUNCVAL( lhttp_start   ) },
    { LSTRKEY( "serverstop"  ),	 LFUNCVAL( lhttp_stop    ) },
	{ LSTRKEY( "get"         ),	 LFUNCVAL( lnet_get      ) },
	{ LSTRKEY( "post"        ),	 LFUNCVAL( lnet_post     ) },
	{ LSTRKEY( "postfile"    ),	 LFUNCVAL( lnet_postfile ) },
	{ LNILKEY, LNILVAL }
};

#endif
