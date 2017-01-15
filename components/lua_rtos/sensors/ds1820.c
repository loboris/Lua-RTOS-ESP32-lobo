/**
 * DS1820 Family temperature sensor driver for Lua-RTOS-ESP32
 * author: LoBo (loboris@gmail.com)
 * based on TM_ONEWIRE (author  Tilen Majerle)
 */

#include "sensors/ds1820.h"
#include "sys/time.h"
#include <stdio.h>
#include "drivers/owire.h"

int ds_parasite_pwr = 0;
uint32_t ds_start_measure_time = 0;

#ifdef DS18B20ALARMFUNC
static unsigned char ow_alarm_device [MAX_ONEWIRE_SENSORS][8];
#endif

//*********************
// TM_DS18B20_Functions
//*********************

//-----------------------------------------------
unsigned char TM_DS18B20_Is(unsigned char *ROM) {
  /* Checks if first byte is equal to DS18B20's family code */
  if ((*ROM == DS18B20_FAMILY_CODE) ||
	  (*ROM == DS18S20_FAMILY_CODE) ||
	  (*ROM == DS1822_FAMILY_CODE)  ||
	  (*ROM == DS28EA00_FAMILY_CODE)) {
    return *ROM;
  }
  return 0;
}

//------------------------------------------------------
void TM_DS18B20_Family(unsigned char code, char *dsfamily) {
  switch (code) {
  	  case DS18B20_FAMILY_CODE:
	  	  sprintf(dsfamily, "DS18B20");
	  	  break;
  	  case DS18S20_FAMILY_CODE:
	  	  sprintf(dsfamily, "DS18S20");
	  	  break;
  	  case DS1822_FAMILY_CODE:
	  	  sprintf(dsfamily, "DS1822");
	  	  break;
  	  case DS28EA00_FAMILY_CODE:
	  	  sprintf(dsfamily, "DS28EA00");
	  	  break;
  	  default:
  		  sprintf(dsfamily, "unknown");
  }
}

/*
//-----------------------------------------------
static owState_t TM_DS18B20_Start(unsigned char *ROM) {
  // Check if device is DS18B20
  if (!TM_DS18B20_Is(ROM)) {
    return owError_Not18b20;
  }
  // Reset line
  if (TM_OneWire_Reset() != 0) {
    return owError_NoDevice;
  }
  // Select ROM number
  TM_OneWire_SelectWithPointer(ROM);
  // Start temperature conversion
  TM_OneWire_WriteByte(DS18B20_CMD_CONVERTTEMP);
  return ow_OK;
}
*/

//--------------------------
void TM_DS18B20_StartAll() {
  // Reset pulse
  if (TM_OneWire_Reset() != 0) return;
  // Skip rom
  TM_OneWire_WriteByte(ONEWIRE_CMD_SKIPROM);
  // Test parasite power
  TM_OneWire_WriteByte(ONEWIRE_CMD_RPWRSUPPLY);
  if (TM_OneWire_ReadBit() == 0) ds_parasite_pwr = 1;
  else ds_parasite_pwr = 0;
  //vm_log_debug("DS18B20 Parasite Pwr = %d", ds_parasite_pwr);

  // Reset pulse
  if (TM_OneWire_Reset() != 0) return;
  // Skip rom
  TM_OneWire_WriteByte(ONEWIRE_CMD_SKIPROM);
  // Start conversion on all connected devices
  TM_OneWire_WriteByte(DS18B20_CMD_CONVERTTEMP);
  if (ds_parasite_pwr) {
	  gpio_set_direction(OW_DEVICE.pin, GPIO_MODE_OUTPUT);
	  gpio_set_level(OW_DEVICE.pin,1);
 	  struct timeval now;
	  gettimeofday(&now, NULL);
	  ds_start_measure_time = now.tv_usec;
  }
}

//------------------------------------------------------------------
owState_t TM_DS18B20_Read(unsigned char *ROM, double *destination) {
  unsigned int temperature;
  unsigned char resolution;
  char digit, minus = 0;
  double decimal;
  unsigned char i = 0;
  unsigned char data[9];
  unsigned char crc;
	
  /* Check if device is DS18B20 */
  if (!TM_DS18B20_Is(ROM)) {
    return owError_Not18b20;
  }
  /* Check if line is released, if it is, then conversion is complete */
  if (!TM_OneWire_ReadBit()) {
    /* Conversion is not finished yet */
    return owError_NotFinished; 
  }
  /* Reset line */
  if (TM_OneWire_Reset() != 0) {
    return owError_NoDevice;
  }
  /* Select ROM number */
  TM_OneWire_SelectWithPointer(ROM);
  /* Read scratchpad command by onewire protocol */
  TM_OneWire_WriteByte(ONEWIRE_CMD_RSCRATCHPAD);

  /* Get data */
  for (i = 0; i < 9; i++) {
    /* Read byte by byte */
    data[i] = TM_OneWire_ReadByte();
  }
  /* Calculate CRC */
  crc = TM_OneWire_CRC8(data, 8);
  /* Check if CRC is ok */
  if (crc != data[8]) {
    /* CRC invalid */
    return owError_BadCRC;
  }

  /* First two bytes of scratchpad are temperature values */
  temperature = data[0] | (data[1] << 8);
  /* Reset line */
  TM_OneWire_Reset();
  if (*ROM != DS18S20_FAMILY_CODE) {
	  /* Check if temperature is negative */
	  if (temperature & 0x8000) {
		/* Two's complement, temperature is negative */
		temperature = ~temperature + 1;
		minus = 1;
	  }
	  /* Get sensor resolution */
	  resolution = ((data[4] & 0x60) >> 5) + 9;
	  /* Store temperature integer digits and decimal digits */
	  digit = temperature >> 4;
	  digit |= ((temperature >> 8) & 0x7) << 4;

	  /* Store decimal digits */
	  switch (resolution) {
		case 9: {
		  decimal = (temperature >> 3) & 0x01;
		  decimal *= (double)DS18B20_DECIMAL_STEPS_9BIT;
		} break;
		case 10: {
		  decimal = (temperature >> 2) & 0x03;
		  decimal *= (double)DS18B20_DECIMAL_STEPS_10BIT;
		} break;
		case 11: {
		  decimal = (temperature >> 1) & 0x07;
		  decimal *= (double)DS18B20_DECIMAL_STEPS_11BIT;
		} break;
		case 12: {
		  decimal = temperature & 0x0F;
		  decimal *= (double)DS18B20_DECIMAL_STEPS_12BIT;
		} break;
		default: {
		  decimal = 0xFF;
		  digit = 0;
		}
	  }

	  /* Check for negative part */
	  decimal = digit + decimal;
	  if (minus) {
		decimal = 0 - decimal;
	  }
	  /* Set to pointer */
	  *destination = decimal;

  }
  else {
	if (!data[7]) {
	    return owError_Convert;
	}
	if (data[1] == 0) {
		temperature = ((int)(data[0] >> 1))*1000;
	}
	else { // negative
		temperature = 1000*(-1*(int)(0x100-data[0]) >> 1);
	}
	temperature -= 250;
	decimal = 1000*((int)(data[7] - data[6]));
	decimal /= (int)data[7];
	temperature += decimal;
    /* Set to pointer */
	*destination = (double)temperature / 1000.0;
  }
  /* Return 1, temperature valid */
  return ow_OK;
}

//----------------------------------------------------------
unsigned char TM_DS18B20_GetResolution(unsigned char *ROM) {
  unsigned char conf;

  if (!TM_DS18B20_Is(ROM)) {
    return owError_Not18b20;
  }

  if (*ROM == DS18S20_FAMILY_CODE) return TM_DS18B20_Resolution_12bits;

  /* Reset line */
  if (TM_OneWire_Reset() != 0) {
    return owError_NoDevice;
  }
  /* Select ROM number */
  TM_OneWire_SelectWithPointer(ROM);
  /* Read scratchpad command by onewire protocol */
  TM_OneWire_WriteByte(ONEWIRE_CMD_RSCRATCHPAD);

  /* Ignore first 4 bytes */
  TM_OneWire_ReadByte();
  TM_OneWire_ReadByte();
  TM_OneWire_ReadByte();
  TM_OneWire_ReadByte();

  /* 5th byte of scratchpad is configuration register */
  conf = TM_OneWire_ReadByte();

  /* Return 9 - 12 value according to number of bits */
  return ((conf & 0x60) >> 5) + 9;
}

//------------------------------------------------------------------------------------------
owState_t TM_DS18B20_SetResolution(unsigned char *ROM, TM_DS18B20_Resolution_t resolution) {
  unsigned char th, tl, conf;
  if (!TM_DS18B20_Is(ROM)) {
    return owError_Not18b20;
  }

  if (*ROM == DS18S20_FAMILY_CODE) return ow_OK;

  /* Reset line */
  if (TM_OneWire_Reset() != 0) {
    return owError_NoDevice;
  }
  /* Select ROM number */
  TM_OneWire_SelectWithPointer(ROM);
  /* Read scratchpad command by onewire protocol */
  TM_OneWire_WriteByte(ONEWIRE_CMD_RSCRATCHPAD);

  /* Ignore first 2 bytes */
  TM_OneWire_ReadByte();
  TM_OneWire_ReadByte();

  th = TM_OneWire_ReadByte();
  tl = TM_OneWire_ReadByte();
  conf = TM_OneWire_ReadByte();

  if (resolution == TM_DS18B20_Resolution_9bits) {
    conf &= ~(1 << DS18B20_RESOLUTION_R1);
    conf &= ~(1 << DS18B20_RESOLUTION_R0);
  } else if (resolution == TM_DS18B20_Resolution_10bits) {
    conf &= ~(1 << DS18B20_RESOLUTION_R1);
    conf |= 1 << DS18B20_RESOLUTION_R0;
  } else if (resolution == TM_DS18B20_Resolution_11bits) {
    conf |= 1 << DS18B20_RESOLUTION_R1;
    conf &= ~(1 << DS18B20_RESOLUTION_R0);
  } else if (resolution == TM_DS18B20_Resolution_12bits) {
    conf |= 1 << DS18B20_RESOLUTION_R1;
    conf |= 1 << DS18B20_RESOLUTION_R0;
  }

  /* Reset line */
  if (TM_OneWire_Reset() != 0) {
    return owError_NoDevice;
  }
  /* Select ROM number */
  TM_OneWire_SelectWithPointer(ROM);
  /* Write scratchpad command by onewire protocol, only th, tl and conf register can be written */
  TM_OneWire_WriteByte(ONEWIRE_CMD_WSCRATCHPAD);

  /* Write bytes */
  TM_OneWire_WriteByte(th);
  TM_OneWire_WriteByte(tl);
  TM_OneWire_WriteByte(conf);

  /* Reset line */
  if (TM_OneWire_Reset() != 0) {
    return owError_NoDevice;
  }
  /* Select ROM number */
  TM_OneWire_SelectWithPointer(ROM);
  /* Copy scratchpad to EEPROM of DS18B20 */
  TM_OneWire_WriteByte(ONEWIRE_CMD_CPYSCRATCHPAD);
  if (ds_parasite_pwr) {
	  gpio_set_direction(OW_DEVICE.pin, GPIO_MODE_OUTPUT);
	  gpio_set_level(OW_DEVICE.pin,1);
  }
  vTaskDelay(20 / portTICK_RATE_MS);
  if (ds_parasite_pwr) {
	  gpio_set_direction(OW_DEVICE.pin, GPIO_MODE_INPUT);
	  gpio_set_level(OW_DEVICE.pin,1);
  }

  return ow_OK;
}

/***************************/
/* DS18B20 Alarm functions */
/***************************/
#ifdef DS18B20ALARMFUNC
//---------------------------------------------------------------------------
static unsigned char TM_DS18B20_SetAlarmLowTemperature(unsigned char *ROM, char temp) {
  unsigned char tl, th, conf;
  if (!TM_DS18B20_Is(ROM)) {
    return owError_Not18b20;
  }
  if (temp > 125) {
    temp = 125;
  } 
  if (temp < -55) {
    temp = -55;
  }
  /* Reset line */
  if (TM_OneWire_Reset() != 0) {
    return owError_NoDevice;
  }
  /* Select ROM number */
  TM_OneWire_SelectWithPointer(ROM);
  /* Read scratchpad command by onewire protocol */
  TM_OneWire_WriteByte(ONEWIRE_CMD_RSCRATCHPAD);

  /* Ignore first 2 bytes */
  TM_OneWire_ReadByte();
  TM_OneWire_ReadByte();

  th = TM_OneWire_ReadByte();
  tl = TM_OneWire_ReadByte();
  conf = TM_OneWire_ReadByte();

  tl = (unsigned char)temp;

  /* Reset line */
  if (TM_OneWire_Reset() != 0) {
    return owError_NoDevice;
  }
  /* Select ROM number */
  TM_OneWire_SelectWithPointer(ROM);
  /* Write scratchpad command by onewire protocol, only th, tl and conf register can be written */
  TM_OneWire_WriteByte(ONEWIRE_CMD_WSCRATCHPAD);

  /* Write bytes */
  TM_OneWire_WriteByte(th);
  TM_OneWire_WriteByte(tl);
  TM_OneWire_WriteByte(conf);

  /* Reset line */
  if (TM_OneWire_Reset() != 0) {
    return owError_NoDevice;
  }
  /* Select ROM number */
  TM_OneWire_SelectWithPointer(ROM);
  /* Copy scratchpad to EEPROM of DS18B20 */
  TM_OneWire_WriteByte(ONEWIRE_CMD_CPYSCRATCHPAD);

  return ow_OK;
}

//------------------------------------------------------------------------------
static owState_t TM_DS18B20_SetAlarmHighTemperature(unsigned char *ROM, char temp) {
  unsigned char tl, th, conf;
  if (!TM_DS18B20_Is(ROM)) {
    return owError_Not18b20;
  }
  if (temp > 125) {
    temp = 125;
  } 
  if (temp < -55) {
    temp = -55;
  }
  /* Reset line */
  if (TM_OneWire_Reset() != 0) {
    return owError_NoDevice;
  }
  /* Select ROM number */
  TM_OneWire_SelectWithPointer(ROM);
  /* Read scratchpad command by onewire protocol */
  TM_OneWire_WriteByte(ONEWIRE_CMD_RSCRATCHPAD);

  /* Ignore first 2 bytes */
  TM_OneWire_ReadByte();
  TM_OneWire_ReadByte();

  th = TM_OneWire_ReadByte();
  tl = TM_OneWire_ReadByte();
  conf = TM_OneWire_ReadByte();

  th = (unsigned char)temp;

  /* Reset line */
  if (TM_OneWire_Reset() != 0) {
    return owError_NoDevice;
  }
  /* Select ROM number */
  TM_OneWire_SelectWithPointer(ROM);
  /* Write scratchpad command by onewire protocol, only th, tl and conf register can be written */
  TM_OneWire_WriteByte(ONEWIRE_CMD_WSCRATCHPAD);

  /* Write bytes */
  TM_OneWire_WriteByte(th);
  TM_OneWire_WriteByte(tl);
  TM_OneWire_WriteByte(conf);

  /* Reset line */
  if (TM_OneWire_Reset() != 0) {
    return owError_NoDevice;
  }
  /* Select ROM number */
  TM_OneWire_SelectWithPointer(ROM);
  /* Copy scratchpad to EEPROM of DS18B20 */
  TM_OneWire_WriteByte(ONEWIRE_CMD_CPYSCRATCHPAD);

  return ow_OK;
}

//-----------------------------------------------------------------
static owState_t TM_DS18B20_DisableAlarmTemperature(unsigned char *ROM) {
  unsigned char tl, th, conf;
  if (!TM_DS18B20_Is(ROM)) {
    return owError_Not18b20;
  }
  /* Reset line */
  if (TM_OneWire_Reset() != 0) {
    return owError_NoDevice;
  }
  /* Select ROM number */
  TM_OneWire_SelectWithPointer(ROM);
  /* Read scratchpad command by onewire protocol */
  TM_OneWire_WriteByte(ONEWIRE_CMD_RSCRATCHPAD);

  /* Ignore first 2 bytes */
  TM_OneWire_ReadByte();
  TM_OneWire_ReadByte();

  th = TM_OneWire_ReadByte();
  tl = TM_OneWire_ReadByte();
  conf = TM_OneWire_ReadByte();

  th = 125;
  tl = (unsigned char)-55;

  /* Reset line */
  if (TM_OneWire_Reset() != 0) {
    return owError_NoDevice;
  }
  /* Select ROM number */
  TM_OneWire_SelectWithPointer(ROM);
  /* Write scratchpad command by onewire protocol, only th, tl and conf register can be written */
  TM_OneWire_WriteByte(ONEWIRE_CMD_WSCRATCHPAD);

  /* Write bytes */
  TM_OneWire_WriteByte(th);
  TM_OneWire_WriteByte(tl);
  TM_OneWire_WriteByte(conf);

  /* Reset line */
  if (TM_OneWire_Reset() != 0) {
    return owError_NoDevice;
  }
  /* Select ROM number */
  TM_OneWire_SelectWithPointer(ROM);
  /* Copy scratchpad to EEPROM of DS18B20 */
  TM_OneWire_WriteByte(ONEWIRE_CMD_CPYSCRATCHPAD);

  return ow_OK;
}

//---------------------------------------
static unsigned char TM_DS18B20_AlarmSearch() {
  /* Start alarm search */
  return TM_OneWire_Search(DS18B20_CMD_ALARMSEARCH);
}
#endif

/*
//-----------------------------------
static unsigned char TM_DS18B20_AllDone() {
  // If read bit is low, then device is not finished yet with calculation temperature
  return TM_OneWire_ReadBit();
}
*/
