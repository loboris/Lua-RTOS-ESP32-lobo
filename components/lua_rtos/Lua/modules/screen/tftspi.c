/* Lua-RTOS-ESP32 TFT module
 * SPI access functions
 * Author: LoBo (loboris@gmail.com, loboris.github)
 *
 * Module supporting SPI TFT displays based on ILI9341 & ST7735 controllers
*/


#include "freertos/FreeRTOS.h"
#include "screen/tftspi.h"
#include "freertos/task.h"
#include "stdio.h"
#include <sys/driver.h>
#include <drivers/gpio.h>
//#include "soc/spi_reg.h"

//#if LUA_USE_TFT
#if CONFIG_LUA_RTOS_LUA_USE_TFT

uint16_t *tft_line = NULL;
uint16_t _width = 320;
uint16_t _height = 240;

spi_device_handle_t disp_spi = NULL;
static spi_device_handle_t ts_spi = NULL;

//This function is called (in irq context!) just before a transmission starts. It will
//set the D/C line to the value indicated in the user field.
static void IRAM_ATTR disp_spi_pre_transfer_callback(spi_transaction_t *t)
{
    int dc=(int)t->user;
    gpio_set_level(PIN_NUM_DC, dc);
}

static spi_bus_config_t buscfg = {0};
static spi_device_interface_config_t disp_devcfg = {0};
static spi_device_interface_config_t ts_devcfg = {0};

static int colstart = 0;
static int rowstart = 0;	// May be overridden in init func

int TFT_type = -1;

//==============================================================================

#define DELAY 0x80

// ======== Low level TFT SPI functions ========================================

// ===========================================================================
// !!IMPORTANT!! spi_device_select & spi_device_deselect must be used in pairs
// ===========================================================================

//Send a command to the TFT.
//-----------------------------
void tft_cmd(const uint8_t cmd)
{
	if (spi_device_select(disp_spi, 0)) return;
    //taskDISABLE_INTERRUPTS();

    disp_spi_transfer_cmd(disp_spi, cmd);

    spi_device_deselect(disp_spi);
	//taskENABLE_INTERRUPTS();
}

//Send command data to the TFT.
//-----------------------------------------
void tft_data(const uint8_t *data, int len)
{
    if (len==0) return;             //no need to send anything

	if (spi_device_select(disp_spi, 0)) return;
	//taskDISABLE_INTERRUPTS();

    spi_transfer_data(disp_spi, (unsigned char *)data, NULL, len, 0);

	spi_device_deselect(disp_spi);
	//taskENABLE_INTERRUPTS();
}

// Draw pixel on TFT on x,y position using given color
//---------------------------------------------------------------
void drawPixel(int16_t x, int16_t y, uint16_t color, uint8_t sel)
{
	if (sel) {
		if (spi_device_select(disp_spi, 0)) return;
	}
	//taskDISABLE_INTERRUPTS();

	// ** Send pixel color **
	disp_spi_set_pixel(disp_spi, x, y, color);

	if (sel) spi_device_deselect(disp_spi);
	//taskENABLE_INTERRUPTS();
}

// Write 'len' 16-bit color data to TFT 'window' (x1,y2),(x2,y2)
// uses the buffer to fill the color values
//---------------------------------------------------------------------------------
void TFT_pushColorRep(int x1, int y1, int x2, int y2, uint16_t color, uint32_t len)
{
	uint16_t ccolor = color;
	if (spi_device_select(disp_spi, 0)) return;
	//vTaskSuspendAll ();

	// ** Send address window **
	disp_spi_transfer_addrwin(disp_spi, x1, x2, y1, y2);

	// ** Send repeated pixel color **
	disp_spi_transfer_color_rep(disp_spi, (uint8_t *)&ccolor, len, 1);

	spi_device_deselect(disp_spi);
    //xTaskResumeAll ();
}

// Write 'len' 16-bit color data to TFT 'window' (x1,y2),(x2,y2) from given buffer
//-------------------------------------------------------------------------
void send_data(int x1, int y1, int x2, int y2, uint32_t len, uint16_t *buf)
{
	if (spi_device_select(disp_spi, 0)) return;
	//vTaskSuspendAll ();

	// ** Send address window **
	disp_spi_transfer_addrwin(disp_spi, x1, x2, y1, y2);

	// ** Send pixel buffer **
	disp_spi_transfer_color_rep(disp_spi, (uint8_t *)buf, len, 0);

	spi_device_deselect(disp_spi);
    //xTaskResumeAll ();
}

// Reads one pixel/color from the TFT's GRAM
//--------------------------------------
uint16_t readPixel(int16_t x, int16_t y)
{
	uint8_t inbuf[4] = {0};

	if (spi_device_select(disp_spi, 0)) return 0;
	//taskDISABLE_INTERRUPTS();

	// ** Send address window **
	disp_spi_transfer_addrwin(disp_spi, x, x+1, y, y+1);

    // ** GET pixel color **
	disp_spi_transfer_cmd(disp_spi, TFT_RAMRD);

	spi_transfer_data(disp_spi, NULL, inbuf, 0, 4);

	spi_device_deselect(disp_spi);
    //taskENABLE_INTERRUPTS();

	//printf("READ DATA: [%02x, %02x, %02x, %02x]\r\n", inbuf[0],inbuf[1],inbuf[2],inbuf[3]);
    return (uint16_t)((uint16_t)((inbuf[1] & 0xF8) << 8) | (uint16_t)((inbuf[2] & 0xFC) << 3) | (uint16_t)(inbuf[3] >> 3));
}

// Reads pixels/colors from the TFT's GRAM
//-------------------------------------------------------------------
int read_data(int x1, int y1, int x2, int y2, int len, uint8_t *buf)
{
	memset(buf, 0, len*2);

	uint8_t *rbuf = malloc((len*3)+1);
    if (!rbuf) return -1;

    memset(rbuf, 0, (len*3)+1);

	if (spi_device_select(disp_spi, 0)) return -2;
	//vTaskSuspendAll ();

    // ** Send address window **
	disp_spi_transfer_addrwin(disp_spi, x1, x2, y1, y2);

    // ** GET pixels/colors **
	disp_spi_transfer_cmd(disp_spi, TFT_RAMRD);

	spi_transfer_data(disp_spi, NULL, rbuf, 0, (len*3)+1);

	spi_device_deselect(disp_spi);
    //xTaskResumeAll ();

    int idx = 0;
    uint16_t color;
    for (int i=1; i<(len*3); i+=3) {
    	color = (uint16_t)((uint16_t)((rbuf[i] & 0xF8) << 8) | (uint16_t)((rbuf[i+1] & 0xFC) << 3) | (uint16_t)(rbuf[i+2] >> 3));
    	buf[idx++] = color >> 8;
    	buf[idx++] = color & 0xFF;
    }
    free(rbuf);

    return 0;
}

//-----------------------------------
uint16_t touch_get_data(uint8_t type)
{
	uint8_t cmd[3] = {0};
	uint8_t rxbuf[3] = {0x55};

	cmd[0] = type;

	if (spi_device_select(ts_spi, 0)) return 0;
	//taskDISABLE_INTERRUPTS();

	spi_transfer_data(ts_spi, cmd, rxbuf, 3, 3);

	spi_device_deselect(ts_spi);
    //taskENABLE_INTERRUPTS();

	//printf("TOUCH: cmd=%02x, data: [%02x %02x %02x]\r\n", type, rxbuf[0], rxbuf[1], rxbuf[2]);
    return (((uint16_t)(rxbuf[1] << 8) | (uint16_t)(rxbuf[2])) >> 4);
}

//======== Display initialization data =========================================

// Initialization commands for 7735B screens
// -----------------------------------------
static const uint8_t Bcmd[] = {
  18,						// 18 commands in list:
  ST7735_SWRESET,   DELAY,	//  1: Software reset, no args, w/delay
  50,						//     50 ms delay
  ST7735_SLPOUT ,   DELAY,	//  2: Out of sleep mode, no args, w/delay
  255,						//     255 = 500 ms delay
  ST7735_COLMOD , 1+DELAY,	//  3: Set color mode, 1 arg + delay:
  0x05,						//     16-bit color 5-6-5 color format
  10,						//     10 ms delay
  ST7735_FRMCTR1, 3+DELAY,	//  4: Frame rate control, 3 args + delay:
  0x00,						//     fastest refresh
  0x06,						//     6 lines front porch
  0x03,						//     3 lines back porch
  10,						//     10 ms delay
  TFT_MADCTL , 1      ,		//  5: Memory access ctrl (directions), 1 arg:
  0x08,						//     Row addr/col addr, bottom to top refresh
  ST7735_DISSET5, 2      ,	//  6: Display settings #5, 2 args, no delay:
  0x15,						//     1 clk cycle nonoverlap, 2 cycle gate
  // rise, 3 cycle osc equalize
  0x02,						//     Fix on VTL
  ST7735_INVCTR , 1      ,	//  7: Display inversion control, 1 arg:
  0x0,						//     Line inversion
  ST7735_PWCTR1 , 2+DELAY,	//  8: Power control, 2 args + delay:
  0x02,						//     GVDD = 4.7V
  0x70,						//     1.0uA
  10,						//     10 ms delay
  ST7735_PWCTR2 , 1      ,	//  9: Power control, 1 arg, no delay:
  0x05,						//     VGH = 14.7V, VGL = -7.35V
  ST7735_PWCTR3 , 2      ,	// 10: Power control, 2 args, no delay:
  0x01,						//     Opamp current small
  0x02,						//     Boost frequency
  ST7735_VMCTR1 , 2+DELAY,	// 11: Power control, 2 args + delay:
  0x3C,						//     VCOMH = 4V
  0x38,						//     VCOML = -1.1V
  10,						//     10 ms delay
  ST7735_PWCTR6 , 2      ,	// 12: Power control, 2 args, no delay:
  0x11, 0x15,
  ST7735_GMCTRP1,16      ,	// 13: Magical unicorn dust, 16 args, no delay:
  0x09, 0x16, 0x09, 0x20,	//     (seriously though, not sure what
  0x21, 0x1B, 0x13, 0x19,	//      these config values represent)
  0x17, 0x15, 0x1E, 0x2B,
  0x04, 0x05, 0x02, 0x0E,
  ST7735_GMCTRN1,16+DELAY,	// 14: Sparkles and rainbows, 16 args + delay:
  0x0B, 0x14, 0x08, 0x1E,	//     (ditto)
  0x22, 0x1D, 0x18, 0x1E,
  0x1B, 0x1A, 0x24, 0x2B,
  0x06, 0x06, 0x02, 0x0F,
  10,						//     10 ms delay
  TFT_CASET  , 4      , 	// 15: Column addr set, 4 args, no delay:
  0x00, 0x02,				//     XSTART = 2
  0x00, 0x81,				//     XEND = 129
  TFT_PASET  , 4      , 	// 16: Row addr set, 4 args, no delay:
  0x00, 0x02,				//     XSTART = 1
  0x00, 0x81,				//     XEND = 160
  ST7735_NORON  ,   DELAY,	// 17: Normal display on, no args, w/delay
  10,						//     10 ms delay
  TFT_DISPON ,   DELAY,  	// 18: Main screen turn on, no args, w/delay
  255						//     255 = 500 ms delay
};

// Init for 7735R, part 1 (red or green tab)
// -----------------------------------------
static const uint8_t  Rcmd1[] = {
  15,						// 15 commands in list:
  ST7735_SWRESET,   DELAY,	//  1: Software reset, 0 args, w/delay
  150,						//     150 ms delay
  ST7735_SLPOUT ,   DELAY,	//  2: Out of sleep mode, 0 args, w/delay
  255,						//     500 ms delay
  ST7735_FRMCTR1, 3      ,	//  3: Frame rate ctrl - normal mode, 3 args:
  0x01, 0x2C, 0x2D,			//     Rate = fosc/(1x2+40) * (LINE+2C+2D)
  ST7735_FRMCTR2, 3      ,	//  4: Frame rate control - idle mode, 3 args:
  0x01, 0x2C, 0x2D,			//     Rate = fosc/(1x2+40) * (LINE+2C+2D)
  ST7735_FRMCTR3, 6      ,	//  5: Frame rate ctrl - partial mode, 6 args:
  0x01, 0x2C, 0x2D,			//     Dot inversion mode
  0x01, 0x2C, 0x2D,			//     Line inversion mode
  ST7735_INVCTR , 1      ,	//  6: Display inversion ctrl, 1 arg, no delay:
  0x07,						//     No inversion
  ST7735_PWCTR1 , 3      ,	//  7: Power control, 3 args, no delay:
  0xA2,
  0x02,						//     -4.6V
  0x84,						//     AUTO mode
  ST7735_PWCTR2 , 1      ,	//  8: Power control, 1 arg, no delay:
  0xC5,						//     VGH25 = 2.4C VGSEL = -10 VGH = 3 * AVDD
  ST7735_PWCTR3 , 2      ,	//  9: Power control, 2 args, no delay:
  0x0A,						//     Opamp current small
  0x00,						//     Boost frequency
  ST7735_PWCTR4 , 2      ,	// 10: Power control, 2 args, no delay:
  0x8A,						//     BCLK/2, Opamp current small & Medium low
  0x2A,
  ST7735_PWCTR5 , 2      ,	// 11: Power control, 2 args, no delay:
  0x8A, 0xEE,
  ST7735_VMCTR1 , 1      ,	// 12: Power control, 1 arg, no delay:
  0x0E,
  TFT_INVOFF , 0      ,		// 13: Don't invert display, no args, no delay
  TFT_MADCTL , 1      ,		// 14: Memory access control (directions), 1 arg:
  0xC0,						//     row addr/col addr, bottom to top refresh, RGB order
  ST7735_COLMOD , 1+DELAY,	//  15: Set color mode, 1 arg + delay:
  0x05,						//     16-bit color 5-6-5 color format
  10						//     10 ms delay
};

// Init for 7735R, part 2 (green tab only)
// ---------------------------------------
static const uint8_t Rcmd2green[] = {
  2,						//  2 commands in list:
  TFT_CASET  , 4      ,		//  1: Column addr set, 4 args, no delay:
  0x00, 0x02,				//     XSTART = 0
  0x00, 0x7F+0x02,			//     XEND = 129
  TFT_PASET  , 4      ,	    //  2: Row addr set, 4 args, no delay:
  0x00, 0x01,				//     XSTART = 0
  0x00, 0x9F+0x01			//     XEND = 160
};

// Init for 7735R, part 2 (red tab only)
// -------------------------------------
static const uint8_t Rcmd2red[] = {
  2,						//  2 commands in list:
  TFT_CASET  , 4      ,	    //  1: Column addr set, 4 args, no delay:
  0x00, 0x00,				//     XSTART = 0
  0x00, 0x7F,				//     XEND = 127
  TFT_PASET  , 4      ,	    //  2: Row addr set, 4 args, no delay:
  0x00, 0x00,				//     XSTART = 0
  0x00, 0x9F				//     XEND = 159
};

// Init for 7735R, part 3 (red or green tab)
// -----------------------------------------
static const uint8_t Rcmd3[] = {
  4,						//  4 commands in list:
  ST7735_GMCTRP1, 16      ,	//  1: Magical unicorn dust, 16 args, no delay:
  0x02, 0x1c, 0x07, 0x12,
  0x37, 0x32, 0x29, 0x2d,
  0x29, 0x25, 0x2B, 0x39,
  0x00, 0x01, 0x03, 0x10,
  ST7735_GMCTRN1, 16      ,	//  2: Sparkles and rainbows, 16 args, no delay:
  0x03, 0x1d, 0x07, 0x06,
  0x2E, 0x2C, 0x29, 0x2D,
  0x2E, 0x2E, 0x37, 0x3F,
  0x00, 0x00, 0x02, 0x10,
  ST7735_NORON  ,    DELAY,	//  3: Normal display on, no args, w/delay
  10,						//     10 ms delay
  TFT_DISPON ,    DELAY,	//  4: Main screen turn on, no args w/delay
  100						//     100 ms delay
};

// Init for ILI7341
// ----------------
static const uint8_t ILI9341_init[] = {
  23,                   					        // 23 commands in list
  ILI9341_SWRESET, DELAY,   						//  1: Software reset, no args, w/delay
  200,												//     50 ms delay
  ILI9341_POWERA, 5, 0x39, 0x2C, 0x00, 0x34, 0x02,
  ILI9341_POWERB, 3, 0x00, 0XC1, 0X30,
  0xEF, 3, 0x03, 0x80, 0x02,
  ILI9341_DTCA, 3, 0x85, 0x00, 0x78,
  ILI9341_DTCB, 2, 0x00, 0x00,
  ILI9341_POWER_SEQ, 4, 0x64, 0x03, 0X12, 0X81,
  ILI9341_PRC, 1, 0x20,
  ILI9341_PWCTR1, 1,  								//Power control
  0x23,               								//VRH[5:0]
  ILI9341_PWCTR2, 1,   								//Power control
  0x10,                 							//SAP[2:0];BT[3:0]
  ILI9341_VMCTR1, 2,    							//VCM control
  0x3e,                 							//Contrast
  0x28,
  ILI9341_VMCTR2, 1,  								//VCM control2
  0x86,
  TFT_MADCTL, 1,    								// Memory Access Control
  0x48,
  ILI9341_PIXFMT, 1,
  0x55,
  ILI9341_FRMCTR1, 2,
  0x00,
  0x18,
  ILI9341_DFUNCTR, 3,   							// Display Function Control
  0x08,
  0x82,
  0x27,
  TFT_PTLAR, 4, 0x00, 0x00, 0x01, 0x3F,
  ILI9341_3GAMMA_EN, 1,								// 3Gamma Function Disable
  0x00, // 0x02
  ILI9341_GAMMASET, 1, 								//Gamma curve selected
  0x01,
  ILI9341_GMCTRP1, 15,   							//Positive Gamma Correction
  0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1,
  0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00,
  ILI9341_GMCTRN1, 15,   							//Negative Gamma Correction
  0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1,
  0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F,
  ILI9341_SLPOUT, DELAY, 							//  Sleep out
  120,			 									//  120 ms delay
  TFT_DISPON, 0,
};

//------------------------------------------------------
// Companion code to the above tables.  Reads and issues
// a series of LCD commands stored in byte array
//--------------------------------------------
static void commandList(const uint8_t *addr) {
  uint8_t  numCommands, numArgs, cmd;
  uint16_t ms;

  numCommands = *addr++;         // Number of commands to follow
  while(numCommands--) {         // For each command...
    cmd = *addr++;               // save command
    numArgs  = *addr++;          //   Number of args to follow
    ms       = numArgs & DELAY;  //   If high bit set, delay follows args
    numArgs &= ~DELAY;           //   Mask out delay bit

    tft_cmd(cmd);
    tft_data(addr, numArgs);

    addr += numArgs;

    if(ms) {
      ms = *addr++;              // Read post-command delay time (ms)
      if(ms == 255) ms = 500;    // If 255, delay for 500 ms
	  vTaskDelay(ms / portTICK_RATE_MS);
    }
  }
}

// Initialization code common to both 'B' and 'R' type displays
//-----------------------------------------------------
static void ST7735_commonInit(const uint8_t *cmdList) {
	// toggle RST low to reset; CS low so it'll listen to us
#ifdef TFT_SOFT_RESET
  tft_cmd(ST7735_SWRESET);
  vTaskDelay(130 / portTICK_RATE_MS);
#else
  TFT_RST1;
  vTaskDelay(10 / portTICK_RATE_MS);
  TFT_RST0;
  vTaskDelay(50 / portTICK_RATE_MS);
  TFT_RST1;
  vTaskDelay(130 / portTICK_RATE_MS);
#endif
  if(cmdList) commandList(cmdList);
}

// Initialization for ST7735B screens
//------------------------------
static void ST7735_initB(void) {
  ST7735_commonInit(Bcmd);
}

// Initialization for ST7735R screens (green or red tabs)
//-----------------------------------------
static void ST7735_initR(uint8_t options) {
  vTaskDelay(50 / portTICK_RATE_MS);
  ST7735_commonInit(Rcmd1);
  if(options == INITR_GREENTAB) {
    commandList(Rcmd2green);
    colstart = 2;
    rowstart = 1;
  } else {
    // colstart, rowstart left at default '0' values
    commandList(Rcmd2red);
  }
  commandList(Rcmd3);

  // if black, change MADCTL color filter
  if (options == INITR_BLACKTAB) {
    tft_cmd(TFT_MADCTL);
    uint8_t dt = 0xC0;
    tft_data(&dt, 1);
  }

  //  tabcolor = options;
}

//---------------------------------------------------------------------------------------------------------------------
void tft_spi_disp_config(unsigned char sdi, unsigned char sdo, unsigned char sck, unsigned char cs, unsigned char dc) {
	buscfg.miso_io_num=sdi;
	buscfg.mosi_io_num=sdo;
	buscfg.sclk_io_num=sck;

    disp_devcfg.clock_speed_hz = 10000000;					// Defoult Clock out at 10 MHz
	disp_devcfg.mode = 0;									// SPI mode 0
	disp_devcfg.spics_io_num = -1;							// use software CS pin
	disp_devcfg.spics_ext_io_num = cs;						// CS pin
	disp_devcfg.spidc_io_num = dc;							// DC pin
	disp_devcfg.queue_size = 7;								// We want to be able to queue 7 transactions at a time
	disp_devcfg.pre_cb = disp_spi_pre_transfer_callback;	// Specify pre-transfer callback to handle D/C line
}

//-----------------------------------------------------------------------------------------------------
void tft_spi_touch_config(unsigned char sdi, unsigned char sdo, unsigned char sck, unsigned char tcs) {
	buscfg.miso_io_num=sdi;
	buscfg.mosi_io_num=sdo;
	buscfg.sclk_io_num=sck;

    ts_devcfg.clock_speed_hz = 2500000;	// Clock out at 2.5 MHz
	ts_devcfg.mode = 0;					// SPI mode 0
	ts_devcfg.spics_io_num = -1;		// CS pin not used
	ts_devcfg.spics_ext_io_num = tcs;	// ext CS pin
	ts_devcfg.spidc_io_num = -1;		// DC pin not used
	ts_devcfg.queue_size = 2;			// We want to be able to queue 2 transactions at a time
}

//-----------------------
void tft_set_defaults() {
	buscfg.miso_io_num = PIN_NUM_MISO;
	buscfg.mosi_io_num = PIN_NUM_MOSI;
	buscfg.sclk_io_num = PIN_NUM_CLK;
	buscfg.quadwp_io_num = -1;
	buscfg.quadhd_io_num = -1;

	disp_devcfg.clock_speed_hz = 10000000;					// Clock out at 10 MHz
	disp_devcfg.mode = 0;									// SPI mode 0
	disp_devcfg.spics_io_num = -1;							// use software CS pin
	disp_devcfg.spics_ext_io_num = PIN_NUM_CS;				// CS pin
	disp_devcfg.spidc_io_num = PIN_NUM_DC;
	disp_devcfg.queue_size = 7;								// We want to be able to queue 7 transactions at a time
	disp_devcfg.pre_cb = disp_spi_pre_transfer_callback;	// Specify pre-transfer callback to handle D/C line
	disp_devcfg.flags = SPI_DEVICE_HALFDUPLEX;
	disp_devcfg.selected = 0;

	ts_devcfg.clock_speed_hz=2500000;			// Clock out at 2.5 MHz
	ts_devcfg.mode = 0;							// SPI mode 0
	ts_devcfg.spics_io_num = -1;				// use software CS pin
	ts_devcfg.spics_ext_io_num = PIN_NUM_TCS;	// CS pin
	ts_devcfg.spidc_io_num = -1;
	ts_devcfg.queue_size = 2;					// We want to be able to queue 2 transactions at a time
	ts_devcfg.selected = 0;
	//ts_devcfg.flags = SPI_DEVICE_HALFDUPLEX;
}

// Init tft SPI interface
//-----------------------------------
esp_err_t tft_spi_init(uint8_t typ) {
	esp_err_t error;

	driver_error_t *err = espi_init(VSPI_HOST, &disp_devcfg, &buscfg, &disp_spi);
	if (err) {
		free(err);
		return -98;
	}
	if (typ == 3) {
		driver_error_t *err = espi_init(VSPI_HOST, &ts_devcfg, &buscfg, &ts_spi);
		if (err) {
			free(err);
			return -99;
		}
	}

	// Test if display device can be selected & deselected
	error = spi_device_select(disp_spi, 0);
	if (error) return error;
	error = spi_device_deselect(disp_spi);
	if (error) return error;

	#ifdef TFT_USE_BKLT
    gpio_set_direction(PIN_NUM_BCKL, GPIO_MODE_OUTPUT);
	#endif

    #ifndef TFT_SOFT_RESET
    gpio_set_direction(PIN_NUM_RST, GPIO_MODE_OUTPUT);
	#endif

    // Initialize display
    if (typ == 0) {
    	ST7735_initB();
    }
    else if (typ == 1) {
    	ST7735_initR(INITR_BLACKTAB);
    }
    else if (typ == 2) {
    	ST7735_initR(INITR_GREENTAB);
    }
    else if (typ == 3) {
		#ifndef TFT_SOFT_RESET
		//Reset the display
		TFT_RST0;
		vTaskDelay(100 / portTICK_RATE_MS);
		TFT_RST0;
		vTaskDelay(100 / portTICK_RATE_MS);
		#endif
        commandList(ILI9341_init);
    }

    ///Enable backlight
	#ifdef TFT_USE_BKLT
    gpio_set_level(PIN_NUM_BCKL, 0);
	#endif

	spi_device_deselect(disp_spi);

	if (!tft_line) tft_line = malloc(TFT_LINEBUF_MAX_SIZE*2);

	return ESP_OK;
}

#endif
