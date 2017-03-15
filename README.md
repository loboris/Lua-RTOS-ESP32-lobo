---

[Clone of the Whitecat's Lua-RTOS-ESP32 repository](https://github.com/whitecatboard/Lua-RTOS-ESP32)

---

###### with some added functionality

### Added modules

* led
  * support for WS2812 - NeoPixel RGB leds

* tft
  * full support for ILI9341 & ST7735 based TFT modules in 4-wire SPI mode.
  * Supported  are many graphics elements, fixed width and proportional fonts (7 included, unlimited number of fonts from file)
  * jpeg, bmp and raw bitmap images.
  * Touch screen supported
  * Read from display memory supported

* cam
  * support for Arducam-Mini-2MP camera module, uses SPI & I2C interfaces
  * jpeg format supported, sizes 176x120 to 1600x1200
  * capture to file or buffer
  * capture directly to tft Display (tft.jpgimage(x,y,scale,"cam") function

* cjson
  * Fast, standards compliant encoding/parsing routines
  * Full support for JSON with UTF-8, including decoding surrogate pairs
  * Optional run-time support for common exceptions to the JSON specification (infinity, NaN,..)
  * No dependencies on other libraries

### Modified modules

* io
  * added support for ymodem file transfer (io.ymsend & io.ymreceive functions)
  * attributes functions returns file/directors type, size and timestamp

* os
  * added function os.exists() for checking file existance
  * added calibration for sleep function
  * datetime preserved after sleep
  * bootreason: added text boot reason description
  * added sleepcalib() function to calibrate sleep time
  * os.resetreason() returns reset reason as numeric and string (descriptive) values
  * added **os.list()** function (enhanced os.ls()): lists file timestamps (sfpiffs&fat), free and total drive space, number of files in directory, match files by wildchard
  * added os.mountfat() & os.unmountfat() functions
  * added os.compile() function to compile lua source file to lua bytecode. Can also list the bytecode, useful for learning how Lua virtual machine works

* i2c
  * added high level functions: send, receive, sendreceive

* spi
  * Based on new espi driver

* net.http
  * start & stop functions renamed to serverstart & serverstop
  * added http client functions get, post, postfile

* sensor
  * added config options do individually enable/disable DS1820 & BME280 sensors
  * support for BME280 temperature, humidity and pressure sensors in I2C mode

* spiffs
  * removed the complete implementation and replaced with slightly different one
  * spiffs source files are unchanged original files prom pellepl's repository
  * added timestamp to files/directories
  * mkspiffs sets files/directories timestamp
  * formating spiffs on startup does not reset the system

* sd card support & fat fs
  * removed the complete implementation and replaced with driver based on esp-idf sdmmc driver
  * sdcard can be connected in 1-bit or 4-bit mode
  * os.mountfat() & os.unmountfat() functions provided to mount sdcard if inserted after boot or change the card

* sleep & boot
  * os.sleep() function improved, time is preserved after sleep
  * sleep time calibration added, os.setsleepcalib() function
  * boot count added and reported at start and available as Lua function
  * boot reason reported on boot and available as Lua function

* ESPI DRIVER:
  * NEW espi driver implemented, based on esp-idf spi_master driver
  * Queued/DMA & direct/nonDMA transfers combined
  * includes special support for display functions


### Other

Added some documentations, lua examples, tools...

---

---

# What's Lua RTOS?

Lua RTOS is a real-time operating system designed to run on embedded systems, with minimal requirements of FLASH and RAM memory. Currently Lua RTOS is available for ESP32, ESP8266 and PIC32MZ platforms, and can be easilly ported to other 32-bit platforms.

Lua RTOS is the main-core of the Whitecat ecosystem, that is being developed by a team of engineers, educators and living lab designers, designed for build Internet Of Things networks in an easy way.

Lua RTOS has a 3-layers design:

1. In the top layer there is a Lua 5.3.4 interpreter which offers to the programmer all resources provided by Lua 5.3.4 programming language, plus special modules for access the hardware (PIO, ADC, I2C, RTC, etc ...) and middleware services provided by Lua RTOS (LoRa WAN, MQTT, ...).

2. In the middle layer there is a Real-Time micro-kernel, powered by FreeRTOS. This is the responsible for that things happen in the expected time.

3. In the bottom layer there is a hardware abstraction layer, which talk directly with the platform hardware.

![](http://whitecatboard.org/git/luaos.png)

For porting Lua RTOS to other platforms is only necessary to write the code for the bottom layer, because the top and the middle layer are the same for all platforms.

# How is it programmed?

The Lua RTOS compatible boards can be programmed in two ways: using the Lua programming language directly, or using a block-based programming language that translates blocks to Lua. No matter if you use Lua or blocks, both forms of programming are made from the same programming environment. The programmer can decide, for example, to made a fast prototype using blocks, then change to Lua, and finally back to blocks.

![](http://whitecatboard.org/wp-content/uploads/2016/11/block-example.png)

![](http://whitecatboard.org/wp-content/uploads/2016/11/code-example.png)

In our [wiki](https://github.com/whitecatboard/Lua-RTOS-ESP32/wiki) you have more information about this.

# How to get Lua RTOS firmware?

## Prerequisites

1. Please note you need probably to download and install drivers for your board's USB-TO-SERIAL adapter for Windows and Mac OSX versions. The GNU/Linux version usually doesn't need any drivers. This drivers are required for connect to your board through a serial port connection.

   | Board              |
   |--------------------|
   | [WHITECAT ESP32 N1](https://www.silabs.com/products/development-tools/software/usb-to-uart-bridge-vcp-drivers)  | 
   | [ESP32 CORE](https://www.silabs.com/products/development-tools/software/usb-to-uart-bridge-vcp-drivers)  | 
   | [ESP32 THING](http://www.ftdichip.com/Drivers/VCP.htm)  | 

## Method 1: get a precompiled firmware

1. Install esptool (the ESP32 flasher utility), following  [this instructions](https://github.com/espressif/esptool).

1. Get the precompiled binaries for your board:

   | Board              |
   |--------------------|
   | [WHITECAT ESP32 N1](http://whitecatboard.org/firmware.php?board=WHITECAT-ESP32-N1)  | 
   | [ESP32 CORE](http://whitecatboard.org/firmware.php?board=ESP32-CORE-BOARD)  | 
   | [ESP32 THING](http://whitecatboard.org/firmware.php?board=ESP32-THING)  | 
   | [GENERIC](http://whitecatboard.org/firmware.php?board=GENERIC)  | 

2. Uncompress to your favorite folder:

   ```lua
   unzip LuaRTOS.10.WHITECAT-ESP32-N1.1488209955.zip
   ```

## Method 2: build by yourself

1. Install ESP32 toolchain for your desktop platform. Please, follow the instructions provided by ESPRESSIF:
   * [Windows](https://github.com/espressif/esp-idf/blob/master/docs/windows-setup.rst)
   * [Mac OS](https://github.com/espressif/esp-idf/blob/master/docs/macos-setup.rst)
   * [Linux](https://github.com/espressif/esp-idf/blob/master/docs/linux-setup.rst)

1. Clone esp-idf repository from ESPRESSIF:

   ```lua
   git clone --recursive https://github.com/espressif/esp-idf.git
   ```

1. Clone Lua RTOS repository:

   ```lua
   git clone --recursive https://github.com/whitecatboard/Lua-RTOS-ESP32
   ```
   
1. Setup the build environment:
   
   Go to Lua-RTOS-ESP32 folder:
   
   ```lua
   cd Lua-RTOS-ESP32
   ```
   
   Edit the env file and change HOST_PLATFORM, PATH, IDF_PATH, LIBRARY_PATH, PKG_CONFIG_PATH, CPATH for fit to your installation locations.
   
   Now do:
   
   ```lua
   source ./env
   ```

1. Set the default configuration for your board:

   | Board              | Run this command                                     |
   |--------------------|------------------------------------------------------|
   | WHITECAT ESP32 N1  | make SDKCONFIG_DEFAULTS=WHITECAT-ESP32-N1 defconfig  |
   | ESP32 CORE         | make SDKCONFIG_DEFAULTS=ESP32-CORE-BOARD defconfig   |
   | ESP32 THING        | make SDKCONFIG_DEFAULTS=ESP32-THING defconfig        |
   | GENERIC            | make SDKCONFIG_DEFAULTS=GENERIC defconfig            |

1. Change the default configuration:

   You can change the default configuration doing:
   
   ```lua
   make menuconfig
   ```
   
   Remember to check the device name for your board's USB-TO-SERIAL adapter under the "Serial flasher config / Default serial port" category.
   
1. Compile:

   Build Lua RTOS, and flash it to your ESP32 board:

   ```lua
   make flash
   ```

   Flash the spiffs file system image to your ESP32 board:
   
   ```lua
   make flashfs
   ```
   
# Connect to the console

You can connect to the Lua RTOS console using your favorite terminal emulator program, such as picocom, minicom, hyperterminal, putty, etc ... The connection parameters are:

   * speed: 115200 bauds
   * data bits: 8
   * stop bits: 1
   * parity: none
   * terminal emulation: VT100

   For example, if you use picocom:
   
   ```lua
   picocom --baud 115200 /dev/tty.SLAB_USBtoUART
   ```
   
   ```lua
   --------------------
   Booting Lua RTOS...

    Boot reason: Deep Sleep reset digital core
     Boot count: 1
     Sleep time: 10 sec
           From: Tue Mar 14 19:28:16 2017
             To: Tue Mar 14 19:28:26 2017

     /\       /\
    /  \_____/  \
   /_____________\
   W H I T E C A T

   Lua RTOS LoBo 0.2 build 1489519645 Copyright (C) 2015 - 2017 whitecatboard.org
   board type ESP32 THING
   cpu ESP32 rev 0 at 240 Mhz
   flash EUI d665503346133812
   spiffs0 start address at 0x180000, size 1024 Kb
   spiffs0 mounted
   Mounting SD Card: OK
   --------------------
    Mode: SPI (1bit)
    Name: NCard
    Type: SDHC/SDXC
   Speed: default speed (25 MHz)
    Size: 15079 MB
     CSD: ver=1, sector_size=512, capacity=30881792 read_bl_len=9
     SCR: sd_spec=2, bus_width=5

   redirecting console messages to file system ...

   Lua RTOS LoBo 0.2 powered by Lua 5.3.4

   Executing /system.lua ...
   Executing /autorun.lua ...

   / > 
   ```
