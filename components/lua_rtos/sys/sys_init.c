/*
 * Lua RTOS, system init
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

#include "luartos.h"
#include "lua.h"
#include "esp_log.h"
#include "esp_vfs.h"
#include "driver/periph_ctrl.h"

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

#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "driver/sdmmc_defs.h"
#include "sdmmc_cmd.h"

//extern void _syscalls_init();
extern void _pthread_init();
extern void _signal_init();
extern void _mtx_init();
extern void _cpu_init();
extern void _clock_init();

extern const char *__progname;

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

static void sdcard_print_info(const sdmmc_card_t* card)
{
	printf("--------------------\r\n");
	#if SD_1BITMODE
    printf(" Mode: SPI (1bit)\r\n");
	#else
    printf(" Mode:  SD (4bit)\r\n");
	#endif
    printf(" Name: %s\r\n", card->cid.name);
    printf(" Type: %s\r\n", (card->ocr & SD_OCR_SDHC_CAP)?"SDHC/SDXC":"SDSC");
    printf("Speed: %s (%d MHz)\r\n", (card->csd.tr_speed > 25000000)?"high speed":"default speed", card->csd.tr_speed/1000000);
    printf(" Size: %u MB\r\n", (uint32_t)(((uint64_t) card->csd.capacity) * card->csd.sector_size / (1024 * 1024)));
    printf("  CSD: ver=%d, sector_size=%d, capacity=%d read_bl_len=%d\r\n",
            card->csd.csd_ver,
            card->csd.sector_size, card->csd.capacity, card->csd.read_block_len);
    printf("  SCR: sd_spec=%d, bus_width=%d\r\n", card->scr.sd_spec, card->scr.bus_width);
}

//------------------
void mount_fatfs() {
#if USE_FAT
    if (mount_is_mounted("fat")) {
    	printf("FAT fs already mounted\r\n");
    	return;
    }
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();

	#if SD_1BITMODE
    // Use 1-line SD mode
    host.flags = SDMMC_HOST_FLAG_1BIT;
	#endif
    // This initializes the slot without card detect (CD) and write protect (WP) signals.
    // Modify slot_config.gpio_cd and slot_config.gpio_wp if your board has these signals.
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();

    // Options for mounting the filesystem.
    // If format_if_mount_failed is set to true, SD card will be partitioned and formatted
    // in case when mounting fails.
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
		#if SD_FORMAT
    	.format_if_mount_failed = true,
		#else
		.format_if_mount_failed = false,
		#endif
		.max_files = SD_MAXFILES
    };

    // Use settings defined above to initialize SD card and mount FAT filesystem.
    // Note: esp_vfs_fat_sdmmc_mount is an all-in-one convenience function.
    // Please check its source code and implement error recovery when developing
    // production applications.
    sdmmc_card_t* card;
	esp_log_level_set("*", ESP_LOG_NONE);
    printf("Mounting SD Card: ");

	esp_err_t ret = esp_vfs_fat_sdmmc_mount("/fat", &host, &slot_config, &mount_config, &card);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
        	printf("Failed to mount filesystem. May be not formated\r\n");
        } else {
        	printf("Failed to initialize. Check connection.\r\n");
        }
    }
    else {
		// Card has been initialized, print its properties
    	printf("OK\r\n");
		sdcard_print_info(card);
        mount_set_mounted("fat", 1);
    }
	esp_log_level_set("*", ESP_LOG_ERROR);

    if (mount_is_mounted("fat")) {
        // Redirect console messages to /log/messages.log ...
        closelog();
        printf("\r\nredirecting console messages to file system ...\r\n");
        openlog(__progname, LOG_NDELAY , LOG_LOCAL1);
    } else {
    	printf("\r\ncan't redirect console messages to file system, an SDCARD is needed\r\n");
    }
#endif
}

//--------------------
void unmount_fatfs() {
#if USE_FAT
    if (mount_is_mounted("fat")) {
    	esp_err_t ret = esp_vfs_fat_sdmmc_unmount();
        if (ret != ESP_OK) {
           	printf("FAT fs was not mounted\r\n");
        }
        mount_set_mounted("fat", 0);

    	printf("FAT fs unmounted\r\n");
    }
    else {
    	printf("FAT fs was not mounted\r\n");
    }
#endif
}


void _sys_init() {
	struct timeval tv;
    struct tm timeinfo;
	time_t now;
	char buf[64] = {'\0'};

	esp_log_level_set("*", ESP_LOG_ERROR);

	// Disable hardware modules modules
	periph_module_disable(PERIPH_LEDC_MODULE);

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

	printf("Booting Lua RTOS... \r\n");
	delay(100);

	// Print some startup info
	console_clear();

	if (sleep_check != SLEEP_CHECK_ID) boot_count = 0;
	else boot_count++;

	cpu_reset_reason(buf);
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
    printf("/______________\\\r\n");
    printf("W H I T E C A T\r\n\r\n");

    printf("Lua RTOS %s build %d Copyright (C) 2015 - 2017 whitecatboard.org\r\n", LUA_OS_VER, BUILD_TIME);

	#ifdef RUN_TESTS
		// Create and run a pthread for tests
		pthread_attr_t attr;
		struct sched_param sched;
		pthread_t thread;
		int res;

		// Init thread attributes
		pthread_attr_init(&attr);

		// Set stack size
	    pthread_attr_setstacksize(&attr, LUA_TASK_STACK);

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

    //Init filesystems
	#if USE_NET_VFS
    	vfs_net_register();
	#endif

    #if USE_SPIFFS
    	vfs_spiffs_register();
    #endif

   	mount_fatfs();
        
    // Continue init ...
    printf("\r\n");
}
