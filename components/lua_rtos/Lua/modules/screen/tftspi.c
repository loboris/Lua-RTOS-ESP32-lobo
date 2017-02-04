/*
 *
 */

#include "freertos/FreeRTOS.h"
#include "screen/tftspi.h"
#include "freertos/task.h"
#include "stdio.h"
#include <sys/driver.h>
#include "sys/syslog.h"
#include <drivers/gpio.h>

#if LUA_USE_TFT

uint8_t tft_line[TFT_LINEBUF_MAX_SIZE] = {0};
uint16_t _width = 320;
uint16_t _height = 240;

static int colstart = 0;
static int rowstart = 0;	// May be overridden in init func

static tft_spi_config_t tft_spi_config = {0};
static tft_spi_config_t touch_spi_config = {0};

//==============================================================================

#define DELAY 0x80
#define SWAPBYTES(i) ((i>>8) | (i<<8))
#define SpiNum_SPI2 2


// ======== Low level TFT SPI functions ========================================

//Send a command to the TFT.
//-----------------------------
void tft_cmd(const uint8_t cmd)
{
	unsigned char command = cmd;

    taskDISABLE_INTERRUPTS();
    gpio_pin_clr(tft_spi_config.dc);
    spi_master_op(tft_spi_config.spi, 1, 1, &command, NULL);
    taskENABLE_INTERRUPTS();
}

//Send data to the TFT.
//-----------------------------------------
void tft_data(const uint8_t *data, int len)
{
    if (len==0) return;             //no need to send anything

    taskDISABLE_INTERRUPTS();
	gpio_pin_set(tft_spi_config.dc);
    spi_master_op(tft_spi_config.spi, 1, len, (unsigned char *)data, NULL);
    taskENABLE_INTERRUPTS();
}

// Draw pixel on TFT on x,y position using given color
//---------------------------------------------------
void drawPixel(int16_t x, int16_t y, uint16_t color)
{
	if((x < 0) ||(x >= _width) || (y < 0) || (y >= _height)) return;

	// ** Send address window **
	uint8_t cmd = TFT_CASET;
	uint32_t data = (x >> 8) | ((x & 0xFF) << 8) | (((x+1) >> 8) << 16) | (((x+1) & 0xFF) << 24);

    taskDISABLE_INTERRUPTS();

    gpio_pin_clr(tft_spi_config.dc);
    spi_master_op(tft_spi_config.spi, 1, 1, &cmd, NULL);

	gpio_pin_set(tft_spi_config.dc);
    spi_master_op(tft_spi_config.spi, 4, 1, (unsigned char *)(&data), NULL);

	cmd = TFT_PASET;
	data = (y >> 8) | ((y & 0xFF) << 8) | (((y+1) >> 8) << 16) | (((y+1) & 0xFF) << 24);
	gpio_pin_clr(tft_spi_config.dc);
    spi_master_op(tft_spi_config.spi, 1, 1, &cmd, NULL);

	gpio_pin_set(tft_spi_config.dc);
    spi_master_op(tft_spi_config.spi, 4, 1, (unsigned char *)(&data), NULL);

    // ** Send pixel color **
	cmd = TFT_RAMWR;
    uint16_t clr = SWAPBYTES(color);
    gpio_pin_clr(tft_spi_config.dc);
    spi_master_op(tft_spi_config.spi, 1, 1, &cmd, NULL);

    gpio_pin_set(tft_spi_config.dc);
    spi_master_op(tft_spi_config.spi, 2, 1, (unsigned char *)(&clr), NULL);

    taskENABLE_INTERRUPTS();
}

// Reads one pixel/color from the TFT's GRAM
//--------------------------------------
uint16_t readPixel(int16_t x, int16_t y)
{
	unsigned char color[2], dummywr[2] = {0};

    // ** Send address window **
	uint8_t cmd = TFT_CASET;
	uint32_t data = (x >> 8) | ((x & 0xFF) << 8) | (((x+1) >> 8) << 16) | (((x+1) & 0xFF) << 24);

    taskDISABLE_INTERRUPTS();

    gpio_pin_clr(tft_spi_config.dc);
    spi_master_op(tft_spi_config.spi, 1, 1, &cmd, NULL);

	gpio_pin_set(tft_spi_config.dc);
    spi_master_op(tft_spi_config.spi, 4, 1, (unsigned char *)(&data), NULL);

	cmd = TFT_PASET;
	data = (y >> 8) | ((y & 0xFF) << 8) | (((y+1) >> 8) << 16) | (((y+1) & 0xFF) << 24);
	gpio_pin_clr(tft_spi_config.dc);
    spi_master_op(tft_spi_config.spi, 1, 1, &cmd, NULL);

	gpio_pin_set(tft_spi_config.dc);
    spi_master_op(tft_spi_config.spi, 4, 1, (unsigned char *)(&data), NULL);

    // ** GET pixel color **
	cmd = TFT_RAMRD;
    gpio_pin_clr(tft_spi_config.dc);
    spi_master_op(tft_spi_config.spi, 1, 1, &cmd, NULL);

    gpio_pin_set(tft_spi_config.dc);
    spi_master_op(tft_spi_config.spi, 1, 2, dummywr, color);

    taskENABLE_INTERRUPTS();

    return (uint16_t)((color[0] << 8) | color[1]);
}

// Write 'len' 16-bit color data to TFT 'window' (x1,y2),(x2,y2)
// uses the buffer to fill the color values
//---------------------------------------------------------------------------------
void TFT_pushColorRep(int x1, int y1, int x2, int y2, uint16_t color, uint32_t len)
{
    uint8_t *buf = NULL;
    int tosend, i, sendlen;
    uint32_t buflen;
	uint8_t cmd;
	uint32_t data;

    if (len == 0) return;
    else if (len <= TFT_MAX_BUF_LEN) buflen = len;
    else buflen = TFT_MAX_BUF_LEN;

    buf = malloc(buflen*2);
    if (!buf) return;

    // fill buffer with pixel color data
	for (i=0;i<(buflen*2);i+=2) {
		buf[i] = (uint8_t)(color >> 8);
		buf[i+1] = (uint8_t)(color & 0x00FF);
	}

	// ** Send address window **
    taskDISABLE_INTERRUPTS();

	cmd = TFT_CASET;
	data = (x1 >> 8) | ((x1 & 0xFF) << 8) | ((x2 >> 8) << 16) | ((x2 & 0xFF) << 24);
    gpio_pin_clr(tft_spi_config.dc);
    spi_master_op(tft_spi_config.spi, 1, 1, &cmd, NULL);
	gpio_pin_set(tft_spi_config.dc);
    spi_master_op(tft_spi_config.spi, 4, 1, (unsigned char *)(&data), NULL);

	cmd = TFT_PASET;
	data = (y1 >> 8) | ((y1 & 0xFF) << 8) | ((y2 >> 8) << 16) | ((y2 & 0xFF) << 24);
	gpio_pin_clr(tft_spi_config.dc);
    spi_master_op(tft_spi_config.spi, 1, 1, &cmd, NULL);
	gpio_pin_set(tft_spi_config.dc);
    spi_master_op(tft_spi_config.spi, 4, 1, (unsigned char *)(&data), NULL);

    // ** Send repeated color **
	cmd = TFT_RAMWR;
    gpio_pin_clr(tft_spi_config.dc);
    spi_master_op(tft_spi_config.spi, 1, 1, &cmd, NULL);

    taskENABLE_INTERRUPTS();

    gpio_pin_set(tft_spi_config.dc);
	tosend = len;
    while (tosend > 0) {
        if (tosend > buflen) sendlen = buflen;
    	else sendlen = tosend;

        taskDISABLE_INTERRUPTS();
        spi_master_op(tft_spi_config.spi, 1, sendlen*2, buf, NULL);
        taskENABLE_INTERRUPTS();

		tosend -= sendlen;
    }

    free(buf);
}

// Write 'len' 16-bit color data to TFT 'window' (x1,y2),(x2,y2) from given buffer
//--------------------------------------------------------------------
void send_data(int x1, int y1, int x2, int y2, int len, uint8_t *buf)
{
	uint8_t cmd;
	uint32_t data;

	// ** Send address window **
    taskDISABLE_INTERRUPTS();

	cmd = TFT_CASET;
	data = (x1 >> 8) | ((x1 & 0xFF) << 8) | ((x2 >> 8) << 16) | ((x2 & 0xFF) << 24);
    gpio_pin_clr(tft_spi_config.dc);
    spi_master_op(tft_spi_config.spi, 1, 1, &cmd, NULL);
	gpio_pin_set(tft_spi_config.dc);
    spi_master_op(tft_spi_config.spi, 4, 1, (unsigned char *)(&data), NULL);

	cmd = TFT_PASET;
	data = (y1 >> 8) | ((y1 & 0xFF) << 8) | ((y2 >> 8) << 16) | ((y2 & 0xFF) << 24);
	gpio_pin_clr(tft_spi_config.dc);
    spi_master_op(tft_spi_config.spi, 1, 1, &cmd, NULL);
	gpio_pin_set(tft_spi_config.dc);
    spi_master_op(tft_spi_config.spi, 4, 1, (unsigned char *)(&data), NULL);

    // ** Send color data prepared in 'buf'  **
	cmd = TFT_RAMWR;
    gpio_pin_clr(tft_spi_config.dc);
    spi_master_op(tft_spi_config.spi, 1, 1, &cmd, NULL);

    gpio_pin_set(tft_spi_config.dc);
    spi_master_op(tft_spi_config.spi, 1, len*2, buf, NULL);

    taskENABLE_INTERRUPTS();
}

// Reads pixels/colors from the TFT's GRAM
//-------------------------------------------------------------------
void read_data(int x1, int y1, int x2, int y2, int len, uint8_t *buf)
{
	uint8_t cmd;
	uint32_t data;

	memset(buf, 0, len*2);

	// ** Send address window **
    taskDISABLE_INTERRUPTS();

	cmd = TFT_CASET;
	data = (x1 >> 8) | ((x1 & 0xFF) << 8) | ((x2 >> 8) << 16) | ((x2 & 0xFF) << 24);
    gpio_pin_clr(tft_spi_config.dc);
    spi_master_op(tft_spi_config.spi, 1, 1, &cmd, NULL);
	gpio_pin_set(tft_spi_config.dc);
    spi_master_op(tft_spi_config.spi, 4, 1, (unsigned char *)(&data), NULL);

	cmd = TFT_PASET;
	data = (y1 >> 8) | ((y1 & 0xFF) << 8) | ((y2 >> 8) << 16) | ((y2 & 0xFF) << 24);
	gpio_pin_clr(tft_spi_config.dc);
    spi_master_op(tft_spi_config.spi, 1, 1, &cmd, NULL);
	gpio_pin_set(tft_spi_config.dc);
    spi_master_op(tft_spi_config.spi, 4, 1, (unsigned char *)(&data), NULL);

    // ** GET pixels/colors **
	cmd = TFT_RAMRD;
    gpio_pin_clr(tft_spi_config.dc);
    spi_master_op(tft_spi_config.spi, 1, 1, &cmd, NULL);

    gpio_pin_set(tft_spi_config.dc);
    spi_master_op(tft_spi_config.spi, 1, 2, buf, buf);

    taskENABLE_INTERRUPTS();
}

//-----------------------------------
uint16_t touch_get_data(uint8_t type)
{
	uint8_t txbuf[4] = {0};
	uint8_t rxbuf[4] = {0};

	txbuf[0]=type;
    taskDISABLE_INTERRUPTS();
    spi_master_op(touch_spi_config.spi, 1, 4, txbuf, rxbuf);
    taskENABLE_INTERRUPTS();

    return (((uint16_t)(rxbuf[1] << 8) | (uint16_t)(rxbuf[2])) >> 4);
}


//---------------------------------------------
void fill_tftline(uint16_t color, uint16_t len)
{
	uint16_t n = len * 2;
	if (n > TFT_LINEBUF_MAX_SIZE) n = TFT_LINEBUF_MAX_SIZE;

	for (uint16_t i=0;i<n;i+=2) {
		tft_line[i] = (uint8_t)(color >> 8);
		tft_line[i+1] = (uint8_t)(color & 0x00FF);
	}
}

//======== Display initialization data =========================================

static const ili_init_cmd_t ili_init_cmds[]={
	#ifndef TFT_USE_RST
	{0x01, {0}, 0},								//  Software reset
	{0x00, {0}, 200},							//  delay 200 ms
	#endif
    {0xCF, {0x00, 0x83, 0X30}, 3},
    {0xED, {0x64, 0x03, 0X12, 0X81}, 4},
    {0xE8, {0x85, 0x01, 0x79}, 3},
    {0xCB, {0x39, 0x2C, 0x00, 0x34, 0x02}, 5},
    {0xF7, {0x20}, 1},
    {0xEA, {0x00, 0x00}, 2},
    {0xC0, {0x26}, 1},
    {0xC1, {0x11}, 1},
    {0xC5, {0x35, 0x3E}, 2},
    {0xC7, {0xBE}, 1},
    {0x36, {0x28}, 1},
    {0x3A, {0x55}, 1},
    {0xB1, {0x00, 0x1B}, 2},
    {0xF2, {0x08}, 1},
    {0x26, {0x01}, 1},
    //{0xE0, {0x1F, 0x1A, 0x18, 0x0A, 0x0F, 0x06, 0x45, 0X87, 0x32, 0x0A, 0x07, 0x02, 0x07, 0x05, 0x00}, 15},
    //{0XE1, {0x00, 0x25, 0x27, 0x05, 0x10, 0x09, 0x3A, 0x78, 0x4D, 0x05, 0x18, 0x0D, 0x38, 0x3A, 0x1F}, 15},
	//Positive Gamma Correction
	{ILI9341_GMCTRP1,		{0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1, 0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00}, 15},
	//Negative Gamma Correction
	{ILI9341_GMCTRN1,		{0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1, 0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F}, 15},
	{0x2A, {0x00, 0x00, 0x00, 0xEF}, 4},
    {0x2B, {0x00, 0x00, 0x01, 0x3f}, 4},
    {0x2C, {0}, 0},
    {0xB7, {0x07}, 1},
    {0xB6, {0x0A, 0x82, 0x27, 0x00}, 4},
    {0x11, {0}, 0x80},
    {0x29, {0}, 0x80},
    {0, {0}, 0xff},
};

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
// a series of LCD commands stored in PROGMEM byte array
//--------------------------------------------
static void commandList(const uint8_t *addr) {
  uint8_t  numCommands, numArgs, cmd;
  uint16_t ms;

  numCommands = *addr++;         // Number of commands to follow
  while(numCommands--) {         // For each command...
    cmd = *addr++;               // save command
    numArgs  = *addr++;          //   Number of args to follow
    ms       = numArgs & DELAY;  //   If hibit set, delay follows args
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

//-------------------------------
static void disp_spi_defaults() {
	tft_spi_config.spi = SpiNum_SPI2;
	tft_spi_config.bits = 8;
	tft_spi_config.dc = PIN_NUM_DC;
	tft_spi_config.speed = 20000;
	tft_spi_config.mode = 0;
	tft_spi_config.resources.sdi = PIN_NUM_MISO;
	tft_spi_config.resources.sdo = PIN_NUM_MOSI;
	tft_spi_config.resources.sck = PIN_NUM_CLK;
	tft_spi_config.resources.cs = PIN_NUM_CS;
	printf("Display default config: sdi=%d/sdo=%d/sck=%d/cs=%d/dc=%d\r\n",
			tft_spi_config.resources.sdi, tft_spi_config.resources.sdo,
			tft_spi_config.resources.sck, tft_spi_config.resources.cs, tft_spi_config.dc);
}

//--------------------------------
static void touch_spi_defaults() {
	touch_spi_config.spi = SpiNum_SPI2;
	touch_spi_config.bits = 8;
	touch_spi_config.dc = 0;
	touch_spi_config.speed = 2500;
	touch_spi_config.mode = 2;
	touch_spi_config.resources.sdi = PIN_NUM_MISO;
	touch_spi_config.resources.sdo = PIN_NUM_MOSI;
	touch_spi_config.resources.sck = PIN_NUM_CLK;
	touch_spi_config.resources.cs = PIN_NUM_TCS;
	printf("Touch default config: sdi=%d/sdo=%d/sck=%d/cs=%d\r\n\r\n",
			touch_spi_config.resources.sdi, touch_spi_config.resources.sdo,
			touch_spi_config.resources.sck, touch_spi_config.resources.cs);
}

//-------------------------------------------------------------
static driver_error_t *tft_init_spi(tft_spi_config_t *config) {
	driver_error_t *error;

	// Pin initialization
	gpio_set_direction(config->resources.sdo, GPIO_MODE_OUTPUT);
	gpio_set_direction(config->resources.sck, GPIO_MODE_OUTPUT);
	gpio_set_direction(config->resources.cs, GPIO_MODE_OUTPUT);
	gpio_set_direction(config->resources.sdi, GPIO_MODE_INPUT);
	gpio_set_pull_mode(config->resources.sdi, GPIO_PULLUP_ONLY);
    if (config->dc) gpio_set_direction(config->dc, GPIO_MODE_OUTPUT);

    if ((GPIO_PIN_MUX_REG[config->resources.sdi] == 0) ||
    	(GPIO_PIN_MUX_REG[config->resources.sdo] == 0) ||
		(GPIO_PIN_MUX_REG[config->resources.sck] == 0) ||
		(GPIO_PIN_MUX_REG[config->resources.cs] == 0)) {
    	return driver_setup_error(SPI_DRIVER, SPI_ERR_CANT_INIT, "cannot mux pin");
    }

	PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[config->resources.sdi], config->spi);
    PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[config->resources.sdo], config->spi);
    PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[config->resources.sck], config->spi);
    PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[config->resources.cs],  config->spi);

    gpio_matrix_in(config->resources.sdi, HSPIQ_IN_IDX,0);
	gpio_matrix_out(config->resources.sdo, HSPID_OUT_IDX,0,0);
	gpio_matrix_out(config->resources.sck, HSPICLK_OUT_IDX,0,0);
	gpio_matrix_out(config->resources.cs, HSPICS0_OUT_IDX,0,0);

	// Lock resources
	if ((error = spi_lock_resources(config->spi, &config->resources))) {
		return error;
	}

	// There are no errors, continue with SPI initialization
	spi_set_mode(config->spi, config->mode);
	config->speed = spi_set_speed(config->spi, config->speed);
	spi_set_cspin(config->spi, config->resources.cs);
	spi_select(config->spi);
	gpio_pin_set(config->resources.cs);

	return NULL;
}

//-----------------------
void tft_set_defaults() {
	disp_spi_defaults();
	touch_spi_defaults();
}

//---------------------------------
driver_error_t *tft_select_disp() {
	return tft_init_spi(&tft_spi_config);
}

//----------------------------------
driver_error_t *tft_select_touch() {
	return tft_init_spi(&touch_spi_config);
}

//-----------------------------------------------
tft_spi_config_t *tft_get_config(uint8_t which) {
	if (which) return &touch_spi_config;
	else return &tft_spi_config;
}

// Init tft SPI interface
//-----------------------------------------
driver_error_t *tft_spi_init(uint8_t typ) {

	driver_error_t *error = tft_init_spi(&tft_spi_config);
	if (error) return error;

    int cmd=0;

	#ifdef TFT_USE_BKLT
    gpio_set_direction(PIN_NUM_BCKL, GPIO_MODE_OUTPUT);
	#endif

    #ifndef TFT_SOFT_RESET
    gpio_set_direction(PIN_NUM_RST, GPIO_MODE_OUTPUT);
	#endif


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
    else if (typ == 4) {
		#ifndef TFT_SOFT_RESET
		//Reset the display
		TFT_RST0;
		vTaskDelay(100 / portTICK_RATE_MS);
		TFT_RST0;
		vTaskDelay(100 / portTICK_RATE_MS);
		#endif
		//Send all the initialization commands
		while (ili_init_cmds[cmd].databytes != 0xff) {
			if (ili_init_cmds[cmd].cmd > 0) {
				tft_cmd(ili_init_cmds[cmd].cmd);
				tft_data(ili_init_cmds[cmd].data, ili_init_cmds[cmd].databytes&0x1F);
				if (ili_init_cmds[cmd].databytes&0x80) {
					vTaskDelay((ili_init_cmds[cmd].databytes&0x7f) / portTICK_RATE_MS);
				}
			}
			else {
				vTaskDelay(ili_init_cmds[cmd].databytes / portTICK_RATE_MS);
			}
			cmd++;
		}
    }

    ///Enable backlight
	#ifdef TFT_USE_BKLT
    gpio_set_level(PIN_NUM_BCKL, 0);
	#endif

    return NULL;
}

#endif
