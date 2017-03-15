/*
 * Lua RTOS, system init
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

#include "sdkconfig.h"
#include "luartos.h"
#include "lua.h"
#include "esp_log.h"
#include "esp_vfs.h"
#include "esp_deep_sleep.h"
#include "driver/periph_ctrl.h"

#include "vfs/fat.h"

#include <vfs.h>
#include <string.h>
#include <stdio.h>

#include <sys/reent.h>
#include <sys/syslog.h>
#include <sys/console.h>
#include <sys/mount.h>
#include <sys/time.h>
#include <sys/driver.h>
#include <sys/delay.h>
#include <sys/status.h>

#include <drivers/cpu.h>
#include <drivers/uart.h>

extern void _pthread_init();
extern void _signal_init();
extern void _mtx_init();
extern void _cpu_init();
extern void _clock_init();

extern const char *__progname;

// Flash unique id
uint8_t flash_unique_id[8];

#ifdef RUN_TESTS
#include <unity.h>

#include <pthread/pthread.h>

void *_sys_tests(void *arg) {
	printf("Running tests ...\r\n\r\n");

	unity_run_all_tests();

	printf("\r\nTests done!");

	for(;;);

	pthread_exit(NULL);
}

#endif

void _sys_init() {
	struct timeval tv;
    struct tm timeinfo;
	time_t now;
	char buf[64] = {'\0'};

	// Set default power down mode for all RTC power domains in deep sleep
	#if CONFIG_LUA_RTOS_DEEP_SLEEP_RTC_PERIPH
	    esp_deep_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
	#else
	    esp_deep_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
	#endif

	#if CONFIG_LUA_RTOS_DEEP_SLEEP_RTC_SLOW_MEM
	    esp_deep_sleep_pd_config(ESP_PD_DOMAIN_RTC_SLOW_MEM, ESP_PD_OPTION_ON);
	#else
	    esp_deep_sleep_pd_config(ESP_PD_DOMAIN_RTC_SLOW_MEM, ESP_PD_OPTION_ON);
	#endif

	#if CONFIG_LUA_RTOS_DEEP_SLEEP_RTC_FAST_MEM
	    esp_deep_sleep_pd_config(ESP_PD_DOMAIN_RTC_FAST_MEM, ESP_PD_OPTION_ON);
	#else
	    esp_deep_sleep_pd_config(ESP_PD_DOMAIN_RTC_FAST_MEM, ESP_PD_OPTION_ON);
	#endif

	esp_log_level_set("*", ESP_LOG_ERROR);

	// Disable hardware modules modules
	periph_module_disable(PERIPH_LEDC_MODULE);

	#if CONFIG_LUA_RTOS_READ_FLASH_UNIQUE_ID
	// Get flash unique id
	uint8_t command[13] = {0x4b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
	uint8_t response[13];

	spi_flash_send_cmd(sizeof(command), command, response);
	memcpy(flash_unique_id, response + 5, sizeof(flash_unique_id));
	#endif

	// Init important things for Lua RTOS
	_clock_init();
	_cpu_init();
    _mtx_init();
    _driver_init();
    _pthread_init();

    status_set(STATUS_SYSCALLS_INITED);

    _signal_init();

	esp_vfs_unregister("/dev/uart");
	vfs_tty_register();

	printf("\r\n--------------------\r\n");
	printf("Booting Lua RTOS...\r\n");
	delay(100);

	//console_clear();

	// Print some startup info, set buut count, set time
	if (sleep_check != SLEEP_CHECK_ID) boot_count = 0;
	else boot_count++;

	cpu_reset_reasons(buf);
	printf("\r\n Boot reason: %s\r\n", buf);
	if (boot_count) printf("  Boot count: %u\r\n", boot_count);

	time(&now);
	if ((sleep_check == SLEEP_CHECK_ID) && (sleep_start_time+sleep_seconds) > now) {
		now = sleep_start_time+sleep_seconds;
		sleep_check = 0;
		printf("  Sleep time: %u sec\r\n", sleep_seconds);
    	localtime_r(&sleep_start_time, &timeinfo);
		printf("        From: %s", asctime(&timeinfo));
    	localtime_r(&now, &timeinfo);
		printf("          To: %s", asctime(&timeinfo));
	}

	localtime_r(&now, &timeinfo);
    // Is time set? If not, tm_year will be (1970 - 1900).
    if (timeinfo.tm_year < (2017 - 1900)) {
        // Time is not set yet
    	now = BUILD_TIME;
    	localtime_r(&now, &timeinfo);
    	printf("Time not set: build time: %s", asctime(&timeinfo));
    }
    tv.tv_sec = now;
	tv.tv_usec = 0;
	settimeofday(&tv, NULL);
	printf("\r\n");

	printf("  /\\       /\\\r\n");
    printf(" /  \\_____/  \\\r\n");
    printf("/_____________\\\r\n");
    printf("W H I T E C A T\r\n\r\n");

    printf("Lua RTOS %s build %d Copyright (C) 2015 - 2017 whitecatboard.org\r\n", LUA_OS_VER, BUILD_TIME);

    printf("board type %s\r\n", LUA_RTOS_BOARD);

	#ifdef RUN_TESTS
		// Create and run a pthread for tests
		pthread_attr_t attr;
		struct sched_param sched;
		pthread_t thread;
		int res;

		// Init thread attributes
		pthread_attr_init(&attr);

		// Set stack size
	    pthread_attr_setstacksize(&attr, CONFIG_LUA_RTOS_LUA_STACK_SIZE);

	    // Set priority
	    sched.sched_priority = LUA_TASK_PRIORITY;
	    pthread_attr_setschedparam(&attr, &sched);

	    // Set CPU
	    cpu_set_t cpu_set = LUA_TASK_CPU;
	    pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &cpu_set);

		// Create thread
		res = pthread_create(&thread, &attr, _sys_tests, NULL);
		if (res) {
			panic("Cannot start tests");
		}

		vTaskDelete(NULL);
	#endif

    openlog(__progname, LOG_CONS | LOG_NDELAY, LOG_LOCAL1);

    cpu_show_info();
    cpu_show_flash_info();

    //Init filesystems
	#if USE_NET_VFS
    	vfs_net_register();
	#endif

    #if USE_SPIFFS
    	vfs_spiffs_register();
    #endif

	#if USE_FAT
    	mount_fatfs();
    #endif
        
    // Continue init ...
    printf("\n");
}
