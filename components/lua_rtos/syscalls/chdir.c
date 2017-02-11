/*
 * Lua RTOS, chdir syscall implementation
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

#include "lauxlib.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <unistd.h>

#include <sys/mount.h>
#include <sys/fcntl.h>

char currdir[PATH_MAX + 1] = "";

int chdir(const char *path) {
    struct stat statb;
    char *ppath;
    char *lpath;
    int statok;

    if (strlen(path) > PATH_MAX) {
        errno = ENAMETOOLONG;
        return -1;
    }

    ppath = mount_resolve_to_physical(path);
    if (strcmp(ppath, "/spiffs/") == 0) {
    	free(ppath);
    	strncpy(currdir, "", PATH_MAX);
    	return 0;
    }

    if (strcmp(ppath, "/fat/") == 0) {
    	free(ppath);
    	strncpy(currdir, "/sd", PATH_MAX);
    	return 0;
    }

    // Not on root
    lpath = mount_resolve_to_logical(ppath);
    if (!lpath) {
    	errno = ENOTDIR;
    	free(lpath);
    	free(ppath);
    	return -1;
    }

    // Check if the path is a directory
	statok = stat(lpath, &statb);
    if (( statok != 0) || (!S_ISDIR(statb.st_mode))) {
    		free(ppath);
        	free(lpath);
            errno = ENOTDIR;
            return -1;
    }

    // Set current directory to logical path
    strncpy(currdir, lpath, PATH_MAX);

    free(lpath);
	free(ppath);

    return 0;
}
