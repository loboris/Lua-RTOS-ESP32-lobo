#!/bin/bash
export HOST_PLATFORM=linux
export PATH=$PATH:/home/LoBo2_Razno/ESP32/xtensa-esp32-elf/bin
#export PATH=$PATH:/home/LoBo2_Razno/ESP32/crosstool-NG/builds/xtensa-esp32-elf/bin
export IDF_PATH=/home/LoBo2_Razno/ESP32/esp-idf


# For boards which don't use the internal regulator, GPIO12 can be pulled high.
# On boards which use the internal regulator and a 3.3V flash chip, GPIO12 should be pulled up high, which is compatible with SD card operation.
# For boards which use 3.3V flash chip, GPIO12 needs to be low at reset.
#    * In this case, internal pullup can be enabled using a `gpio_pullup_en(GPIO_NUM_12);` call. Most SD cards work fine when an internal pullup on GPIO12 line is enabled. Note that if ESP32 experiences a power-on reset while the SD card is sending data, high level on GPIO12 can be latched into the bootstrapping register, and ESP32 will enter a boot loop until external reset with correct GPIO12 level is applied.
#    * Another option is to program flash voltage selection efuses: set `XPD_SDIO_TIEH=1`, `XPD_SDIO_FORCE=1`, and `XPD_SDIO_REG = 1`. This will permanently select 3.3V output voltage for the internal regulator, and GPIO12 will not be used as a bootstrapping pin anymore. Then it is safe to connect a pullup resistor to GPIO12. This option is suggested for production use.

# The following commands can be used to program flash voltage selection efuses **to 3.3V**:

# Override flash regulator configuration using efuses
/home/LoBo2_Razno/ESP32/esp-idf/components/esptool_py/esptool/espefuse.py --port /dev/ttyUSB0 burn_efuse XPD_SDIO_FORCE
# Select 3.3V output voltage
/home/LoBo2_Razno/ESP32/esp-idf/components/esptool_py/esptool/espefuse.py --port /dev/ttyUSB0 burn_efuse XPD_SDIO_TIEH
# Enable internal voltage regulator
/home/LoBo2_Razno/ESP32/esp-idf/components/esptool_py/esptool/espefuse.py --port /dev/ttyUSB0 burn_efuse XPD_SDIO_REG
