/*
 * Lua RTOS, FAT vfs operations
 *
 * Copyright (C) 2015 - 2017 LoBo
 *
 * Author: LoBo (loboris@gmail.com / https://github.com/loboris)
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
#include <sys/driver.h>
#include <string.h>
#include <stdio.h>
#include <vfs.h>
#include <sys/mount.h>
#include <sys/syslog.h>
#include "esp_log.h"

#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "driver/sdmmc_defs.h"
#include "sdmmc_cmd.h"

#if USE_FAT

extern const char *__progname;

// SDCARD errors
#define SDCRD_ERR_CANT_INIT                (DRIVER_EXCEPTION_BASE(SDCRD_DRIVER_ID) |  0)

//----------------------------------------------------------------
driver_error_t *sdcard_lock_resources(int host, void *resources) {
    driver_unit_lock_error_t *lock_error = NULL;

    // Lock this pins
    if ((lock_error = driver_lock(SDCRD_DRIVER, 1, GPIO_DRIVER, 2))) {
    	return driver_lock_error(SPI_DRIVER, lock_error);
    }
    if ((lock_error = driver_lock(SDCRD_DRIVER, 1, GPIO_DRIVER, 13))) {
    	return driver_lock_error(SPI_DRIVER, lock_error);
    }
    if ((lock_error = driver_lock(SDCRD_DRIVER, 1, GPIO_DRIVER, 14))) {
    	return driver_lock_error(SPI_DRIVER, lock_error);
    }
    if ((lock_error = driver_lock(SDCRD_DRIVER, 1, GPIO_DRIVER, 15))) {
    	return driver_lock_error(SPI_DRIVER, lock_error);
    }
	#if !SD_1BITMODE
    if ((lock_error = driver_lock(SDCRD_DRIVER, 1, GPIO_DRIVER, 4))) {
    	return driver_lock_error(SPI_DRIVER, lock_error);
    }
    if ((lock_error = driver_lock(SDCRD_DRIVER, 1, GPIO_DRIVER, 12))) {
    	return driver_lock_error(SPI_DRIVER, lock_error);
    }
	#endif
	return NULL;
}

DRIVER_REGISTER_ERROR(SDCRD, sdcrd, CannotSetup, "can't setup",  SDCRD_ERR_CANT_INIT);
DRIVER_REGISTER(SDCRD, sdcrd, NULL, NULL, sdcard_lock_resources);

//-----------------------------------------------------
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

#endif


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
        // Lock resources used by CDCard
        driver_error_t *error = sdcard_lock_resources(1, NULL);
        if (error) {
        	printf("Error locking resources\r\n");
        	free(error);
        }

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
