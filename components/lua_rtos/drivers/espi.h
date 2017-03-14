/*
 * Lua RTOS, ESPI driver
 *
 * Copyright (C) 2015 - 2017
 *
 * Author: LoBo (loboris@gmail.com, https://github.com/loboris )
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
 * and fitness.  In no spi shall the author be liable for any
 * special, indirect or consequential damages or any damages
 * whatsoever resulting from loss of use, data or profits, whether
 * in an action of contract, negligence or other tortious action,
 * arising out of or in connection with the use or performance of
 * this software.
 */


#ifndef _ESPI_H_
#define _ESPI_H_

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <sys/driver.h>



#ifdef __cplusplus
extern "C"
{
#endif



/**
 * @brief Enum with the three SPI peripherals that are software-accessible in it
 */
typedef enum {
    SPI_HOST=0,                     ///< SPI1, SPI
    HSPI_HOST=1,                    ///< SPI2, HSPI
    VSPI_HOST=2                     ///< SPI3, VSPI
} spi_host_device_t;


/**
 * @brief This is a configuration structure for a SPI bus.
 *
 * You can use this structure to specify the GPIO pins of the bus. Normally, the driver will use the
 * GPIO matrix to route the signals. An exception is made when all signals either can be routed through 
 * the IO_MUX or are -1. In that case, the IO_MUX is used, allowing for >40MHz speeds.
 */
typedef struct {
    int mosi_io_num;                ///< GPIO pin for Master Out Slave In (=spi_d) signal, or -1 if not used.
    int miso_io_num;                ///< GPIO pin for Master In Slave Out (=spi_q) signal, or -1 if not used.
    int sclk_io_num;                ///< GPIO pin for Spi CLocK signal, or -1 if not used.
    int quadwp_io_num;              ///< GPIO pin for WP (Write Protect) signal which is used as D2 in 4-bit communication modes, or -1 if not used.
    int quadhd_io_num;              ///< GPIO pin for HD (HolD) signal which is used as D3 in 4-bit communication modes, or -1 if not used.
} spi_bus_config_t;


#define SPI_DEVICE_TXBIT_LSBFIRST          (1<<0)  ///< Transmit command/address/data LSB first instead of the default MSB first
#define SPI_DEVICE_RXBIT_LSBFIRST          (1<<1)  ///< Receive data LSB first instead of the default MSB first
#define SPI_DEVICE_BIT_LSBFIRST            (SPI_TXBIT_LSBFIRST|SPI_RXBIT_LSBFIRST); ///< Transmit and receive LSB first
#define SPI_DEVICE_3WIRE                   (1<<2)  ///< Use spiq for both sending and receiving data
#define SPI_DEVICE_POSITIVE_CS             (1<<3)  ///< Make CS positive during a transaction instead of negative
#define SPI_DEVICE_HALFDUPLEX              (1<<4)  ///< Transmit data before receiving it, instead of simultaneously
#define SPI_DEVICE_CLK_AS_CS               (1<<5)  ///< Output clock on CS line if CS is active

#define SPI_ERR_OTHER_CONFIG 7001

typedef struct spi_transaction_t spi_transaction_t;
typedef void(*transaction_cb_t)(spi_transaction_t *trans);

/**
 * @brief This is a configuration for a SPI slave device that is connected to one of the SPI buses.
 */
typedef struct {
    uint8_t command_bits;           ///< Amount of bits in command phase (0-16)
    uint8_t address_bits;           ///< Amount of bits in address phase (0-64)
    uint8_t dummy_bits;             ///< Amount of dummy bits to insert between address and data phase
    uint8_t mode;                   ///< SPI mode (0-3)
    uint8_t duty_cycle_pos;         ///< Duty cycle of positive clock, in 1/256th increments (128 = 50%/50% duty). Setting this to 0 (=not setting it) is equivalent to setting this to 128.
    uint8_t cs_ena_pretrans;        ///< Amount of SPI bit-cycles the cs should be activated before the transmission (0-16). This only works on half-duplex transactions.
    uint8_t cs_ena_posttrans;       ///< Amount of SPI bit-cycles the cs should stay active after the transmission (0-16)
    int clock_speed_hz;             ///< Clock speed, in Hz
    int spics_io_num;               ///< CS GPIO pin for this device, or -1 if not used
    int spics_ext_io_num;           ///< CS GPIO pin for this device used externaly, if spics_io_num=-1
    int spidc_io_num;               ///< DC GPIO pin for this device used by some functions
    uint32_t flags;                 ///< Bitwise OR of SPI_DEVICE_* flags
    int queue_size;                 ///< Transaction queue size. This sets how many transactions can be 'in the air' (queued using spi_device_queue_trans but not yet finished using spi_device_get_trans_result) at the same time
    transaction_cb_t pre_cb;        ///< Callback to be called before a transmission is started. This callback is called within interrupt context.
    transaction_cb_t post_cb;       ///< Callback to be called after a transmission has completed. This callback is called within interrupt context.
    uint8_t selected;               ///< 1 if the device's CS pin is active
} spi_device_interface_config_t;


#define SPI_TRANS_MODE_DIO            (1<<0)  ///< Transmit/receive data in 2-bit mode
#define SPI_TRANS_MODE_QIO            (1<<1)  ///< Transmit/receive data in 4-bit mode
#define SPI_TRANS_MODE_DIOQIO_ADDR    (1<<2)  ///< Also transmit address in mode selected by SPI_MODE_DIO/SPI_MODE_QIO
#define SPI_TRANS_USE_RXDATA          (1<<2)  ///< Receive into rx_data member of spi_transaction_t instead into memory at rx_buffer.
#define SPI_TRANS_USE_TXDATA          (1<<3)  ///< Transmit tx_data member of spi_transaction_t instead of data at tx_buffer. Do not set tx_buffer when using this.

/**
 * This structure describes one SPI transaction
 */
struct spi_transaction_t {
    uint32_t flags;                 ///< Bitwise OR of SPI_TRANS_* flags
    uint16_t command;               ///< Command data. Specific length was given when device was added to the bus.
    uint64_t address;               ///< Address. Specific length was given when device was added to the bus.
    size_t length;                  ///< Total data length, in bits
    size_t rxlength;                ///< Total data length received, if different from length. (0 defaults this to the value of ``length``)
    void *user;                     ///< User-defined variable. Can be used to store eg transaction ID.
    union {
        const void *tx_buffer;      ///< Pointer to transmit buffer, or NULL for no MOSI phase
        uint8_t tx_data[4];         ///< If SPI_USE_TXDATA is set, data set here is sent directly from this variable.
    };
    union {
        void *rx_buffer;            ///< Pointer to receive buffer, or NULL for no MISO phase
        uint8_t rx_data[4];         ///< If SPI_USE_RXDATA is set, data is received directly to this variable
    };
};

typedef struct spi_device_t* spi_device_handle_t;  ///< Handle for a device on a SPI bus

typedef struct {
	spi_host_device_t bus;
	spi_device_handle_t spi;
	spi_device_interface_config_t devcfg;
	spi_bus_config_t buscfg;
	uint8_t data_bits;
	uint8_t duplex;
} espi_userdata;


// ==== Queued DMA transfer ========================================================================

/**
 * @brief Queue a SPI transaction for execution
 *
 * @param handle Device handle obtained using spi_host_add_dev
 * @param trans_desc Description of transaction to execute
 * @param ticks_to_wait Ticks to wait until there's room in the queue; use portMAX_DELAY to
 *                      never time out.
 * @return 
 *         - ESP_ERR_INVALID_ARG   if parameter is invalid
 *         - ESP_OK                on success
 */
esp_err_t spi_device_queue_trans(spi_device_handle_t handle, spi_transaction_t *trans_desc, TickType_t ticks_to_wait);


/**
 * @brief Get the result of a SPI transaction queued earlier
 *
 * This routine will wait until a transaction to the given device (queued earlier with 
 * spi_device_queue_trans) has succesfully completed. It will then return the description of the
 * completed transaction so software can inspect the result and e.g. free the memory or 
 * re-use the buffers.
 *
 * @param handle Device handle obtained using spi_host_add_dev
 * @param trans_desc Pointer to variable able to contain a pointer to the description of the 
 *                   transaction that is executed
 * @param ticks_to_wait Ticks to wait until there's a returned item; use portMAX_DELAY to never time
                        out.
 * @return 
 *         - ESP_ERR_INVALID_ARG   if parameter is invalid
 *         - ESP_OK                on success
 */
esp_err_t spi_device_get_trans_result(spi_device_handle_t handle, spi_transaction_t **trans_desc, TickType_t ticks_to_wait);


/**
 * @brief Do a SPI transaction
 *
 * Essentially does the same as spi_device_queue_trans followed by spi_device_get_trans_result. Do
 * not use this when there is still a transaction queued that hasn't been finalized 
 * using spi_device_get_trans_result.
 *
 * @param handle Device handle obtained using spi_host_add_dev
 * @param trans_desc Pointer to variable able to contain a pointer to the description of the 
 *                   transaction that is executed
 * @return 
 *         - ESP_ERR_INVALID_ARG   if parameter is invalid
 *         - ESP_OK                on success
 */
esp_err_t spi_device_transmit(spi_device_handle_t handle, spi_transaction_t *trans_desc);


// ==== Non queued not DMA transfer and common functions ===========================================

/**
 * @brief Return the actuall SPI bus speed for the spi device in Hz
 *
 * Some frequencies cannot be set, for example 30000000 will 
 * will actually set SPI clock to 26666666 Hz
 *
 * @param handle Device handle obtained using spi_host_add_dev
 * 
 * @return 
 *         - actuall SPI clock
 */
uint32_t espi_get_speed(spi_device_handle_t handle);

/**
 * @brief Set new clock speed for the device, return the actuall SPI bus speed set in Hz
 *
 * Some frequencies cannot be set, for example 30000000 will 
 * will actually set SPI clock to 26666666 Hz
 *
 * @param handle Device handle obtained using spi_host_add_dev
 * @param speed  New device spi clock to be set in Hz
 * 
 * @return 
 *         - actuall SPI clock
 */
uint32_t espi_set_speed(spi_device_handle_t handle, uint32_t speed);

/**
 * @brief Select spi device for transmission when not using Queued transmissions
 *
 * It configures spi bus with selected spi device parameters if previously 
 * selected device was different than current
 * If device's spics_io_num=-1 and spics_ext_io_num > 0
 * 'spics_ext_io_num pin is set to active state (low)
 *
 * @param handle Device handle obtained using spi_host_add_dev
 * @param force  configure spi bus even if the previous device was the same
 * 
 * @return 
 *         - ESP_ERR_INVALID_ARG   if parameter is invalid
 *         - ESP_OK                on success
 */
esp_err_t spi_device_select(spi_device_handle_t handle, int force);

/**
 * @brief De-select spi device for transmission when not using Queued transmissions
 *
 * If device's spics_io_num=-1 and spics_ext_io_num > 0
 * 'spics_ext_io_num pin is set to inactive state (high)
 * 
 * @param handle Device handle obtained using spi_host_add_dev
 * 
 * @return 
 *         - ESP_ERR_INVALID_ARG   if parameter is invalid
 *         - ESP_OK                on success
 */
esp_err_t spi_device_deselect(spi_device_handle_t handle);


/**
 * @brief Check if spi bus uses native spi pins
 *
 * @param handle Device handle obtained using spi_host_add_dev
 * 
 * @return 
 *         - true        if native spi pins are used
 *         - false       if spi pins are routed through gpio matrix
 */
bool spi_uses_native_pins(spi_device_handle_t handle);

void spi_get_native_pins(int host, int *sdi, int *sdo, int *sck);

esp_err_t spi_device_TakeSemaphore(spi_device_handle_t handle);
void spi_device_GiveSemaphore(spi_device_handle_t handle);

/**
 * @brief Send 8-bit data to spi device from output buffer 'data' (wrlen bytes)
 *        and receive data to input buffer 'indata' (rdlen bytes)
 *        If device is in duplex mode, data are read while transmiting,
 *        otherwise data are read after sending
 *
 * @param data     pointer to send data buffer
 * @param wrlen    number of bytes to send
 * @param indata   pointer to receive data buffer
 * @param wrlen    number of bytes to receive
 *
 * @return
 *
 */
void spi_transfer_data(spi_device_handle_t handle, uint8_t *data, uint8_t *indata, uint32_t wrlen, uint32_t rdlen);

driver_error_t *espi_init(spi_host_device_t host, spi_device_interface_config_t *dev_config, spi_bus_config_t *bus_config, spi_device_handle_t *handle);

// ==== Display specific functions ==============

void disp_spi_transfer_cmd(spi_device_handle_t handle, int8_t cmd);
void disp_spi_transfer_addrwin(spi_device_handle_t handle, uint16_t x1, uint16_t x2, uint16_t y1, uint16_t y2);
void disp_spi_set_pixel(spi_device_handle_t handle, uint16_t x, uint16_t y, uint16_t color);
void disp_spi_transfer_pixel(spi_device_handle_t handle, uint16_t color);
void disp_spi_transfer_color_rep(spi_device_handle_t handle, uint8_t *color, uint32_t len, uint8_t rep);


#ifdef __cplusplus
}
#endif

#endif
