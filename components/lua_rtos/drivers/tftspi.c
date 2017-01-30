
#include "drivers/tftspi.h"
#include "freertos/task.h"
#include "stdio.h"

static uint8_t is_init = 0;
spi_device_handle_t tft_spi;
spi_device_handle_t touch_spi;
static spi_transaction_t trans[TFT_NUM_TRANS];
uint8_t queued = 0;
uint8_t tft_line[TFT_LINEBUF_MAX_SIZE] = {0};
static int colstart = 0;
static int rowstart = 0;				// May be overridden in init func

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

#define DELAY 0x80

// Initialization commands for 7735B screens
// -----------------------------------------
static const uint8_t Bcmd[] = {
  18,				// 18 commands in list:
  ST7735_SWRESET,   DELAY,	//  1: Software reset, no args, w/delay
  50,				//     50 ms delay
  ST7735_SLPOUT ,   DELAY,	//  2: Out of sleep mode, no args, w/delay
  255,				//     255 = 500 ms delay
  ST7735_COLMOD , 1+DELAY,	//  3: Set color mode, 1 arg + delay:
  0x05,				//     16-bit color 5-6-5 color format
  10,				//     10 ms delay
  ST7735_FRMCTR1, 3+DELAY,	//  4: Frame rate control, 3 args + delay:
  0x00,				//     fastest refresh
  0x06,				//     6 lines front porch
  0x03,				//     3 lines back porch
  10,				//     10 ms delay
  TFT_MADCTL , 1      ,	//  5: Memory access ctrl (directions), 1 arg:
  0x08,				//     Row addr/col addr, bottom to top refresh
  ST7735_DISSET5, 2      ,	//  6: Display settings #5, 2 args, no delay:
  0x15,				//     1 clk cycle nonoverlap, 2 cycle gate
  //     rise, 3 cycle osc equalize
  0x02,				//     Fix on VTL
  ST7735_INVCTR , 1      ,	//  7: Display inversion control, 1 arg:
  0x0,				//     Line inversion
  ST7735_PWCTR1 , 2+DELAY,	//  8: Power control, 2 args + delay:
  0x02,				//     GVDD = 4.7V
  0x70,				//     1.0uA
  10,				//     10 ms delay
  ST7735_PWCTR2 , 1      ,	//  9: Power control, 1 arg, no delay:
  0x05,				//     VGH = 14.7V, VGL = -7.35V
  ST7735_PWCTR3 , 2      ,	// 10: Power control, 2 args, no delay:
  0x01,				//     Opamp current small
  0x02,				//     Boost frequency
  ST7735_VMCTR1 , 2+DELAY,	// 11: Power control, 2 args + delay:
  0x3C,				//     VCOMH = 4V
  0x38,				//     VCOML = -1.1V
  10,				//     10 ms delay
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
  10,				//     10 ms delay
  TFT_CASET  , 4      , 	// 15: Column addr set, 4 args, no delay:
  0x00, 0x02,			//     XSTART = 2
  0x00, 0x81,			//     XEND = 129
  TFT_RASET  , 4      , 	// 16: Row addr set, 4 args, no delay:
  0x00, 0x02,			//     XSTART = 1
  0x00, 0x81,			//     XEND = 160
  ST7735_NORON  ,   DELAY,	// 17: Normal display on, no args, w/delay
  10,				//     10 ms delay
  TFT_DISPON ,   DELAY,  	// 18: Main screen turn on, no args, w/delay
  255				//     255 = 500 ms delay
};

// Init for 7735R, part 1 (red or green tab)
// -----------------------------------------
static const uint8_t  Rcmd1[] = {
  15,				// 15 commands in list:
  ST7735_SWRESET,   DELAY,	//  1: Software reset, 0 args, w/delay
  150,				//     150 ms delay
  ST7735_SLPOUT ,   DELAY,	//  2: Out of sleep mode, 0 args, w/delay
  255,				//     500 ms delay
  ST7735_FRMCTR1, 3      ,	//  3: Frame rate ctrl - normal mode, 3 args:
  0x01, 0x2C, 0x2D,		//     Rate = fosc/(1x2+40) * (LINE+2C+2D)
  ST7735_FRMCTR2, 3      ,	//  4: Frame rate control - idle mode, 3 args:
  0x01, 0x2C, 0x2D,		//     Rate = fosc/(1x2+40) * (LINE+2C+2D)
  ST7735_FRMCTR3, 6      ,	//  5: Frame rate ctrl - partial mode, 6 args:
  0x01, 0x2C, 0x2D,		//     Dot inversion mode
  0x01, 0x2C, 0x2D,		//     Line inversion mode
  ST7735_INVCTR , 1      ,	//  6: Display inversion ctrl, 1 arg, no delay:
  0x07,				//     No inversion
  ST7735_PWCTR1 , 3      ,	//  7: Power control, 3 args, no delay:
  0xA2,
  0x02,				//     -4.6V
  0x84,				//     AUTO mode
  ST7735_PWCTR2 , 1      ,	//  8: Power control, 1 arg, no delay:
  0xC5,				//     VGH25 = 2.4C VGSEL = -10 VGH = 3 * AVDD
  ST7735_PWCTR3 , 2      ,	//  9: Power control, 2 args, no delay:
  0x0A,				//     Opamp current small
  0x00,				//     Boost frequency
  ST7735_PWCTR4 , 2      ,	// 10: Power control, 2 args, no delay:
  0x8A,				//     BCLK/2, Opamp current small & Medium low
  0x2A,
  ST7735_PWCTR5 , 2      ,	// 11: Power control, 2 args, no delay:
  0x8A, 0xEE,
  ST7735_VMCTR1 , 1      ,	// 12: Power control, 1 arg, no delay:
  0x0E,
  TFT_INVOFF , 0      ,	// 13: Don't invert display, no args, no delay
  TFT_MADCTL , 1      ,	// 14: Memory access control (directions), 1 arg:
  0xC0,				//     row addr/col addr, bottom to top refresh, RGB order
  ST7735_COLMOD , 1+DELAY,	//  15: Set color mode, 1 arg + delay:
  0x05,				//     16-bit color 5-6-5 color format
  10				//     10 ms delay
};

// Init for 7735R, part 2 (green tab only)
// ---------------------------------------
static const uint8_t Rcmd2green[] = {
  2,				//  2 commands in list:
  TFT_CASET  , 4      ,	        //  1: Column addr set, 4 args, no delay:
  0x00, 0x02,			//     XSTART = 0
  0x00, 0x7F+0x02,		//     XEND = 129
  TFT_RASET  , 4      ,	        //  2: Row addr set, 4 args, no delay:
  0x00, 0x01,			//     XSTART = 0
  0x00, 0x9F+0x01		//     XEND = 160
};

// Init for 7735R, part 2 (red tab only)
// -------------------------------------
static const uint8_t Rcmd2red[] = {
  2,				//  2 commands in list:
  TFT_CASET  , 4      ,	        //  1: Column addr set, 4 args, no delay:
  0x00, 0x00,			//     XSTART = 0
  0x00, 0x7F,			//     XEND = 127
  TFT_RASET  , 4      ,	        //  2: Row addr set, 4 args, no delay:
  0x00, 0x00,			//     XSTART = 0
  0x00, 0x9F			//     XEND = 159
};

// Init for 7735R, part 3 (red or green tab)
// -----------------------------------------
static const uint8_t Rcmd3[] = {
  4,				//  4 commands in list:
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
  10,				//     10 ms delay
  TFT_DISPON ,    DELAY,	//  4: Main screen turn on, no args w/delay
  100				//     100 ms delay
};

// Init for ILI7341
// ----------------
static const uint8_t ILI9341_init[] = {
  23,                           // 23 commands in list
  ILI9341_SWRESET, DELAY,   	//  1: Software reset, no args, w/delay
  200,				//     50 ms delay
  ILI9341_POWERA, 5, 0x39, 0x2C, 0x00, 0x34, 0x02,
  ILI9341_POWERB, 3, 0x00, 0XC1, 0X30,
  0xEF, 3, 0x03, 0x80, 0x02,
  ILI9341_DTCA, 3, 0x85, 0x00, 0x78,
  ILI9341_DTCB, 2, 0x00, 0x00,
  ILI9341_POWER_SEQ, 4, 0x64, 0x03, 0X12, 0X81,
  ILI9341_PRC, 1, 0x20,
  ILI9341_PWCTR1, 1,    //Power control
  0x23,                 //VRH[5:0]
  ILI9341_PWCTR2, 1,    //Power control
  0x10,                 //SAP[2:0];BT[3:0]
  ILI9341_VMCTR1, 2,    //VCM control
  0x3e,                 //Contrast
  0x28,
  ILI9341_VMCTR2, 1,    //VCM control2
  0x86,
  TFT_MADCTL, 1,    // Memory Access Control
  0x48,
  ILI9341_PIXFMT, 1,
  0x55,
  ILI9341_FRMCTR1, 2,
  0x00,
  0x18,
  ILI9341_DFUNCTR, 3,    // Display Function Control
  0x08,
  0x82,
  0x27,
  TFT_PTLAR, 4, 0x00, 0x00, 0x01, 0x3F,
  ILI9341_3GAMMA_EN, 1,  // 3Gamma Function Disable
  0x00, // 0x02
  ILI9341_GAMMASET, 1,   //Gamma curve selected
  0x01,
  ILI9341_GMCTRP1, 15,   //Positive Gamma Correction
  0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1,
  0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00,
  ILI9341_GMCTRN1, 15,   //Negative Gamma Correction
  0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1,
  0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F,
  ILI9341_SLPOUT, DELAY, //  Sleep out
  120,			 //  120 ms delay
  TFT_DISPON, 0,
};

//Send a command to the TFT. Uses spi_device_transmit, which waits until the transfer is complete.
//-----------------------------
void tft_cmd(const uint8_t cmd)
{
    esp_err_t ret;
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));       //Zero out the transaction
    t.length=8;                     //Command is 8 bits
    t.tx_buffer=&cmd;               //The data is the cmd itself
    t.user=(void*)0;                //D/C needs to be set to 0
    ret=spi_device_transmit(tft_spi, &t);  //Transmit!
    assert(ret==ESP_OK);            //Should have had no issues.
}

//Send data to the TFT. Uses spi_device_transmit, which waits until the transfer is complete.
//-----------------------------------------
void tft_data(const uint8_t *data, int len)
{
    esp_err_t ret;
    spi_transaction_t t;
    if (len==0) return;             //no need to send anything
    memset(&t, 0, sizeof(t));       //Zero out the transaction
    t.length=len*8;                 //Len is in bytes, transaction length is in bits.
    t.tx_buffer=data;               //Data
    t.user=(void*)1;                //D/C needs to be set to 1
    ret=spi_device_transmit(tft_spi, &t);  //Transmit!
    assert(ret==ESP_OK);            //Should have had no issues.
}

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

/*
 * This function is called (in irq context!) just before a transmission starts. It will
 * set the D/C line to the value indicated in the user field.
 */
//------------------------------------------------------
void tft_spi_pre_transfer_callback(spi_transaction_t *t)
{
    int dc=(int)t->user & 1;
    gpio_set_level(PIN_NUM_DC, dc);
}

//---------------------------
esp_err_t  touch_spi_init() {
	esp_err_t ret;
	spi_bus_config_t buscfg={
		.miso_io_num= PIN_NUM_MISO,
		.mosi_io_num=PIN_NUM_MOSI,
		.sclk_io_num=PIN_NUM_CLK,
		.quadwp_io_num=-1,
		.quadhd_io_num=-1
	};
	spi_device_interface_config_t devcfg={
		.clock_speed_hz=20000000,               //Clock out at 20 MHz
		.mode=3,                                //SPI mode 3
		.spics_io_num=PIN_NUM_TCS,              //Touch CS pin
		.queue_size=TFT_NUM_TRANS,              //We want to be able to queue multiple transactions at a time
		.pre_cb=NULL,
	};
	//Initialize the SPI bus
	ret=spi_bus_initialize(HSPI_HOST, &buscfg, 1);
	if (ret != ESP_OK) return ret;

	//Attach the LCD to the SPI bus
	ret=spi_bus_add_device(HSPI_HOST, &devcfg, &tft_spi);
	if (ret != ESP_OK) return ret;
	is_init = 1;
    return ESP_OK;
}

//--------------------------
esp_err_t  tft_spi_close() {
    if (is_init) {
        esp_err_t ret;
    	ret = spi_bus_remove_device(tft_spi);
		if (ret != ESP_OK) return ret;
    	ret = spi_bus_free(HSPI_HOST);
		if (ret != ESP_OK) return ret;
    }
	return ESP_OK;
}

//---------------------
esp_err_t  tft_init_spi() {
    if (!is_init) {
        esp_err_t ret;
		spi_bus_config_t buscfg={
			.miso_io_num= -1, //PIN_NUM_MISO,
			.mosi_io_num=PIN_NUM_MOSI,
			.sclk_io_num=PIN_NUM_CLK,
			.quadwp_io_num=-1,
			.quadhd_io_num=-1
		};
		spi_device_interface_config_t devcfg={
			.clock_speed_hz=40000000,               //Clock out at 20 MHz
			.mode=0,                                //SPI mode 0
			.spics_io_num=PIN_NUM_CS,               //CS pin
			.queue_size=TFT_NUM_TRANS,              //We want to be able to queue multiple transactions at a time
			.pre_cb=tft_spi_pre_transfer_callback,  //Specify pre-transfer callback to handle D/C line
		};
		//Initialize the SPI bus
		ret=spi_bus_initialize(HSPI_HOST, &buscfg, 1);
		if (ret != ESP_OK) return ret;

		//Attach the LCD to the SPI bus
		ret=spi_bus_add_device(HSPI_HOST, &devcfg, &tft_spi);
		if (ret != ESP_OK) return ret;
		is_init = 1;
    }
    return ESP_OK;
}

// Init tft SPI interface
//-----------------------------------
esp_err_t tft_spi_init(uint8_t typ) {
	esp_err_t ret = tft_init_spi();
	if (ret != ESP_OK) return ret;

    int cmd=0;
    //Initialize non-SPI GPIOs
    gpio_set_direction(PIN_NUM_DC, GPIO_MODE_OUTPUT);

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

    return ESP_OK;
}

/*
 * To send a data we have to send a command, 2 data bytes, another command, 2 more data bytes and another command
 * before sending the data itself; a total of 6 transactions. (We can't put all of this in just one transaction
 * because the D/C line needs to be toggled in the middle.)
 * This routine queues these commands up so they get sent as quickly as possible.
*/
//-------------------------------------------------------------------
void send_data(int x1, int y1, int x2, int y2, int len, uint8_t *buf)
{
    esp_err_t ret;
    int x;
    //Transaction descriptors. Declared static so they're not allocated on the stack; we need this memory even when this
    //function is finished because the SPI driver needs access to it even while we're already calculating the next line.

    //In theory, it's better to initialize trans and data only once and hang on to the initialized
    //variables. We allocate them on the stack, so we need to re-init them each call.
    for (x=0; x<6; x++) {
        memset(&trans[x], 0, sizeof(spi_transaction_t));
        if ((x&1)==0) {
            //Even transfers are commands
            trans[x].length=8;
            trans[x].user=(void*)0;
        } else {
            //Odd transfers are data
            trans[x].length=8*4;
            trans[x].user=(void*)1;
        }
        trans[x].flags=SPI_TRANS_USE_TXDATA;
    }
    trans[0].tx_data[0]=0x2A;		//Column Address Set
    trans[1].tx_data[0]=x1 >> 8;	//Start Col High
    trans[1].tx_data[1]=x1 & 0xff;	//Start Col Low
    trans[1].tx_data[2]=x2 >> 8;	//End Col High
    trans[1].tx_data[3]=x2 & 0xff;	//End Col Low
    trans[2].tx_data[0]=0x2B;		//Page address set
    trans[3].tx_data[0]=y1 >> 8;	//Start page high
    trans[3].tx_data[1]=y1 & 0xff;	//start page low
    trans[3].tx_data[2]=y2 >> 8;	//end page high
    trans[3].tx_data[3]=y2 & 0xff;	//end page low
    trans[4].tx_data[0]=0x2C;		//memory write
    if (len > 2) {
		trans[5].tx_buffer=buf;		//finally send the line data
		trans[5].length = len*2*8;	//Data length, in bits
		trans[5].flags=0;			//undo SPI_TRANS_USE_TXDATA flag
    }
    else {
        for (x=0; x<(len*2); x++) {
        	trans[5].tx_data[x]=buf[x];
        }
		trans[5].length = len*2*8;	//Data length, in bits
    }

    //Queue all transactions.
    for (x=0; x<6; x++) {
        ret=spi_device_queue_trans(tft_spi, &trans[x], portMAX_DELAY);
        assert(ret==ESP_OK);
    }
    queued = 1;
    /*
     * When we are here, the SPI driver is busy (in the background) getting the transactions sent. That happens
     * mostly using DMA, so the CPU doesn't have much to do here. We're not going to wait for the transaction to
     * finish because we may as well spend the time calculating the next line. When that is done, we can call
     * send_line_finish, which will wait for the transfers to be done and check their status.
    */
}

//---------------------
void send_data_finish()
{
    spi_transaction_t *rtrans;
    esp_err_t ret;
    //Wait for all 6 transactions to be done and get back the results.
    for (int x=0; x<6; x++) {
        ret=spi_device_get_trans_result(tft_spi, &rtrans, portMAX_DELAY);
        assert(ret==ESP_OK);
        //We could inspect rtrans now if we received any info back. The LCD is treated as write-only, though.
    }
    queued = 0;
}

//----------------------------------------------------------------------------------
void _TFT_pushColorRep(int x1, int y1, int x2, int y2, uint16_t color, uint32_t len)
{
    uint8_t *buf = NULL;
    esp_err_t ret;
    int tosend, i, sendlen;
    spi_transaction_t *rtrans;
    uint32_t buflen;

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

	tosend = len;
	if (tosend <= buflen) sendlen = len;
	else sendlen = buflen;
	//printf("Send %d pixels at (%d,%d),(%d,%d) buflen=%d, sendlen=%d\r\n", len,x1,y1,x2,y2,buflen,sendlen);
	// Send address & first block of data
	send_data(x1, y1, x2, y2, sendlen, buf);
	send_data_finish();
	tosend -= sendlen;

	if (tosend <= 0) {
		// only one block to send
	    free(buf);
		return;
	}

	// ** continue sending data **
	// prepare transaction data
	memset(&trans[0], 0, sizeof(spi_transaction_t));
	trans[0].user = (void*)1;
    trans[0].flags = 0;
    trans[0].tx_buffer = buf;
    // send all remaining blocks
    while (tosend > 0) {
        if (tosend > buflen) sendlen = buflen;
    	else sendlen = tosend;

    	//printf("continue send: remain %d, sendlen=%d\r\n", tosend,sendlen);
    	trans[0].length = sendlen*2*8; //Data length, in bits
	    ret=spi_device_queue_trans(tft_spi, &trans[0], portMAX_DELAY);
	    assert(ret==ESP_OK);
        ret=spi_device_get_trans_result(tft_spi, &rtrans, portMAX_DELAY);
        assert(ret==ESP_OK);
		tosend -= sendlen;
    }

    free(buf);
}
