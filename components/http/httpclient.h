#ifndef _HTTP_CLIENT_H__
#define _HTTP_CLIENT_H__
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define HTTP_DEBUG 0

#define HTTP_REQ_GET      0
#define HTTP_REQ_HEAD     1
#define HTTP_REQ_POST     2
#define HTTP_REQ_PUT      3
#define HTTP_REQ_DELETE   4
#define HTTP_REQ_TRACE    5
#define HTTP_REQ_OPTIONS  6
#define HTTP_REQ_CONNECT  7
#define HTTP_REQ_PATCH    8

#define HTTP_FILE_ID	  "_#FiLe#_"

typedef int socket_t;
typedef uint32_t http_req_t;

typedef enum
{
  HTTP_SUCCESS = 0,
  HTTP_EMPTY_BODY,
  HTTP_ERR_OPENING_SOCKET,
  HTTP_ERR_DISSECT_ADDR,
  HTTP_ERR_NO_SUCH_HOST,
  HTTP_ERR_CONNECTING,
  HTTP_ERR_WRITING,
  HTTP_ERR_READING,
  HTTP_ERR_OUT_OF_MEM,
  HTTP_ERR_BAD_HEADER,
  HTTP_ERR_TOO_MANY_REDIRECTS,
  HTTP_ERR_IS_HTTPS,
  HTTP_ERR_FILEWRITTING
} http_ret_t;


typedef struct 
{
  char* content_type;
  char* encoding;
  uint16_t status_code;
  char* status_text;
  char* redirect_addr;
  char* content;
} http_header_t;

typedef struct
{
  http_header_t* p_header;
  uint32_t length;
  char* contents;
  http_ret_t status;
} http_response_t;

char *http_boundary;

/**
* @brief: Make a HTTP request to the given server address
* @param address: address you want to request
* @param http_req: HTTP request type, any of the HTTP_REQ_* defines in the module
* @param header_lines: Array of additional HTTP header lines to be added to the header.
* A NULL pointer indicates no additional header lines.
* @param header_line_count: number of elements in header_lines array. 
*/
http_response_t* http_request(char* const address, const http_req_t http_req, char* header_lines);

/**
* @brief: Make a HTTP request with a body. Works exactly like http_request()
* @param body: Body to be added to HTTP request
*/
http_response_t* http_request_w_body(char* const address, const http_req_t http_req, char* header_lines, char* body);

/**
* @brief: Destructor for http_response_t structs.
* @param p_http_resp: http_response_t struct pointer to be free'd
*/
void http_response_free(http_response_t* p_http_resp);

void print_status(http_ret_t status, char *buf);

#endif
