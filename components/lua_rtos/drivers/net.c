/*
 * Lua RTOS, network manager
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
 * and fitness.  In no event shall the author be liable for any
 * special, indirect or consequential damages or any damages
 * whatsoever resulting from loss of use, data or profits, whether
 * in an action of contract, negligence or other tortious action,
 * arising out of or in connection with the use or performance of
 * this software.
 */

#include <stdio.h>
#include <errno.h>
#include <string.h>

#include <drivers/net.h>

// This macro gets a reference for this driver into drivers array
#define NET_DRIVER driver_get_by_name("net")

// Driver message errors
DRIVER_REGISTER_ERROR(NET, net, NotAvailable, "network is not available", NET_ERR_NOT_AVAILABLE);

driver_error_t *net_check_connectivity() {
	if (!NETWORK_AVAILABLE()) {
		return driver_operation_error(NET_DRIVER, NET_ERR_NOT_AVAILABLE,NULL);
	}

	return NULL;
}

driver_error_t *net_lookup(const char *name, struct sockaddr_in *address) {
	driver_error_t *error;
	int rc = 0;

	if ((error = net_check_connectivity())) return error;

	sa_family_t family = AF_INET;
	struct addrinfo *result = NULL;
	struct addrinfo hints = {0, AF_INET, SOCK_STREAM, IPPROTO_TCP, 0, NULL, NULL, NULL};

	if ((rc = getaddrinfo(name, NULL, &hints, &result)) == 0) {
		struct addrinfo *res = result;
		while (res) {
			if (res->ai_family == AF_INET) {
				result = res;
				break;
			}
			res = res->ai_next;
		}

		if (result->ai_family == AF_INET) {
			address->sin_port = htons(0);
			address->sin_family = family = AF_INET;
			address->sin_addr = ((struct sockaddr_in*)(result->ai_addr))->sin_addr;
		}

		freeaddrinfo(result);

		return NULL;
	} else {
		printf("net_lookup error %d, errno %d (%s)\r\n",rc, errno, strerror(rc));
		return NULL;
	}
}

driver_error_t *net_get(const char *name, const char *page, char *response) {
	driver_error_t *error;
	int rc = 0;
	int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	struct sockaddr_in serverAddress;
	char sendline[256], recvline[256];

	size_t n;

	if ((error = net_check_connectivity())) return error;

	if ((error = net_lookup(name, &serverAddress))) {
		return error;
	}

	//inet_pton(AF_INET, address.sin_addr.s_addr, &serverAddress.sin_addr.s_addr);
	serverAddress.sin_port = htons(80);

	rc = connect(sock, (struct sockaddr *)&serverAddress, sizeof(struct sockaddr_in));
	if (rc) {
		rc = close(sock);
		printf("net connect error %d, errno %d (%s)\r\n",rc, errno, strerror(rc));
		return NULL;
	}

	/// Form request
	char poststr[64] = {'\0'};
	snprintf(sendline, 255,
	     "GET %s HTTP/1.0\r\n"  // POST or GET, both tested and works. Both HTTP 1.0 HTTP 1.1 works, but sometimes
	     "Host: %s\r\n"     // but sometimes HTTP 1.0 works better in localhost type
	     "Content-type: application/x-www-form-urlencoded\r\n"
	     "Content-length: %d\r\n\r\n"
	     "%s\r\n", page, name, (unsigned int)strlen(poststr), poststr);

	//rc = write(sock, sendline, strlen(sendline));
	rc = send(sock, sendline, strlen(sendline), 0);
	if (rc <= 0) {
		rc = close(sock);
		printf("net send error %d, errno %d (%s)\r\n",rc, errno, strerror(rc));
		return NULL;
	}

    /// Read the response
    while ((n = read(sock, recvline, 255)) > 0) {
        recvline[n] = '\0';

        printf("%s", recvline);
    }

	rc = close(sock);
	return NULL;
}

DRIVER_REGISTER(NET,net,NULL,NULL,NULL);
