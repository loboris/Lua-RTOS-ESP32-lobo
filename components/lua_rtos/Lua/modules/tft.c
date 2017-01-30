/* Lua-RTOS-ESP32 TFT module

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "drivers/tftspi.h"
#include "time.h"
#include "tjpgd.h"
#include <math.h>

#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
#include "auxmods.h"
#include "error.h"

#define LUA_USE_TFT 1


typedef struct {
	uint8_t 	*font;
	uint8_t 	x_size;
	uint8_t 	y_size;
	uint8_t	    offset;
	uint16_t	numchars;
    uint8_t     bitmap;
	uint16_t    color;
} Font;

typedef struct {
      uint8_t charCode;
      int adjYOffset;
      int width;
      int height;
      int xOffset;
      int xDelta;
      uint16_t dataPtr;
} propFont;

typedef struct {
	uint16_t        x1;
	uint16_t        y1;
	uint16_t        x2;
	uint16_t        y2;
} dispWin_t;

static dispWin_t dispWin = {
  .x1 = 0,
  .y1 = 0,
  .x2 = 320,
  .y2 = 240,
};


extern uint8_t tft_DefaultFont[];

static uint8_t tp_initialized = 0;	// touch panel initialized flag

static int TFT_type = -1;
static uint8_t *userfont = NULL;
static uint8_t orientation = PORTRAIT;	// screen orientation
static uint8_t rotation = 0;			// font rotation

static uint16_t	_width = 320;
static uint16_t	_height = 240;
static uint8_t	_transparent = 0;
static uint16_t	_fg = TFT_GREEN;
static uint16_t _bg = TFT_BLACK;
static uint8_t	_wrap = 0;				// character wrapping to new line
static int		TFT_X  = 0;
static int		TFT_Y  = 0;
static int		TFT_OFFSET  = 0;

static Font		cfont;
static propFont	fontChar;
static uint8_t	_forceFixed = 0;

uint32_t tp_calx = 0x00010001;
uint32_t tp_caly = 0x00010001;

// ================ Basics drawing functions ===================================

//----------------------------------------------------------------
static void TFT_queuePixel(int16_t x, int16_t y, uint16_t color) {
	if ((x < dispWin.x1) || (y < dispWin.y1) || (x >= dispWin.x2) || (y >= dispWin.y2)) return;

	tft_line[0] = (uint8_t)(color >> 8);
	tft_line[1] = (uint8_t)(color & 0x00FF);

	if (queued) send_data_finish();
	send_data(x, y, x+1, y+1, 1, tft_line);
}

//-------------------------------------------------------------------------------
static void TFT_queueFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) {
	// clipping
	if ((x < dispWin.x1) || (x >= dispWin.x2) || (y >= dispWin.y2)) return;
	if (y < dispWin.y1) y = dispWin.y1;
	if ((y + h) > dispWin.y2) h = dispWin.y2 - y;

	for (int i=0; i < (h*2); i+=2) {
		tft_line[i] = (uint8_t)(color >> 8);
		tft_line[i+1] = (uint8_t)(color & 0x00FF);
	}
	if (queued) send_data_finish();
	send_data(x, y, x, y+h-1, (uint32_t)h, tft_line);
}

//-------------------------------------------------------------------------------
static void TFT_queueFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) {
	// clipping
	if ((y < dispWin.y1) || (x >= dispWin.x2) || (y >= dispWin.y2)) return;
	if (x < dispWin.x1) x = dispWin.x1;
	if ((x + w) > dispWin.x2) w = dispWin.x2 - x;

	for (int i=0; i < (w*2); i+=2) {
		tft_line[i] = (uint8_t)(color >> 8);
		tft_line[i+1] = (uint8_t)(color & 0x00FF);
	}
	if (queued) send_data_finish();
	send_data(x, y, x+w-1, y, (uint32_t)w, tft_line);
}


// draw color pixel on screen
//---------------------------------------------------------------
static void TFT_drawPixel(int16_t x, int16_t y, uint16_t color) {

  if ((x < dispWin.x1) || (y < dispWin.y1) || (x >= dispWin.x2) || (y >= dispWin.y2)) return;

  _TFT_pushColorRep(x,y,x+1,y+1, color, 1);
}

//------------------------------------------------------------------------------
static void TFT_drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) {
	// clipping
	if ((x < dispWin.x1) || (x >= dispWin.x2) || (y >= dispWin.y2)) return;
	if (y < dispWin.y1) y = dispWin.y1;
	if ((y + h) > dispWin.y2) h = dispWin.y2 - y;

	_TFT_pushColorRep(x, y, x, y+h-1, color, (uint32_t)h);
}

//------------------------------------------------------------------------------
static void TFT_drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) {
	// clipping
	if ((y < dispWin.y1) || (x >= dispWin.x2) || (y >= dispWin.y2)) return;
	if (x < dispWin.x1) x = dispWin.x1;
	if ((x + w) > dispWin.x2) w = dispWin.x2 - x;

	_TFT_pushColorRep(x, y, x+w-1, y, color, (uint32_t)w);
}

// Bresenham's algorithm - thx wikipedia - speed enhanced by Bodmer this uses
// the eficient FastH/V Line draw routine for segments of 2 pixels or more
//--------------------------------------------------------------------------------------
static void TFT_drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color)
{
  int steep = 0;
  if (abs(y1 - y0) > abs(x1 - x0)) steep = 1;
  if (steep) {
    swap(x0, y0);
    swap(x1, y1);
  }
  if (x0 > x1) {
    swap(x0, x1);
    swap(y0, y1);
  }

  int16_t dx = x1 - x0, dy = abs(y1 - y0);;
  int16_t err = dx >> 1, ystep = -1, xs = x0, dlen = 0;

  if (y0 < y1) ystep = 1;

  // Split into steep and not steep for FastH/V separation
  if (steep) {
    for (; x0 <= x1; x0++) {
      dlen++;
      err -= dy;
      if (err < 0) {
        err += dx;
        if (dlen == 1) TFT_queuePixel(y0, xs, color);
        else TFT_queueFastVLine(y0, xs, dlen, color);
        dlen = 0; y0 += ystep; xs = x0 + 1;
      }
    }
    if (dlen) TFT_queueFastVLine(y0, xs, dlen, color);
  }
  else
  {
    for (; x0 <= x1; x0++) {
      dlen++;
      err -= dy;
      if (err < 0) {
        err += dx;
        if (dlen == 1) TFT_queuePixel(xs, y0, color);
        else TFT_queueFastHLine(xs, y0, dlen, color);
        dlen = 0; y0 += ystep; xs = x0 + 1;
      }
    }
    if (dlen) TFT_queueFastHLine(xs, y0, dlen, color);
  }
  if (queued) send_data_finish();
}

// fill a rectangle
//------------------------------------------------------------------------------------
static void TFT_fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
	// clipping
	if ((x >= dispWin.x2) || (y >= dispWin.y2)) return;

	if (x < dispWin.x1) x = dispWin.x1;
	if (y < dispWin.y1) y = dispWin.y1;

	if ((x + w) > dispWin.x2) w = dispWin.x2 - x;
	if ((y + h) > dispWin.y2) h = dispWin.y2 - y;
	if (w == 0) w = 1;
	if (h == 0) h = 1;
	_TFT_pushColorRep(x, y, x+w-1, y+h-1, color, (uint32_t)(h*w));
}

//------------------------------------------
static void TFT_fillScreen(uint16_t color) {
	_TFT_pushColorRep(0, 0, _width-1, _height-1, color, (uint32_t)(_height*_width));
}

//---------------------------------------------------------------------------------------
static void TFT_drawRect(uint16_t x1,uint16_t y1,uint16_t w,uint16_t h, uint16_t color) {
  TFT_queueFastHLine(x1,y1,w, color);
  TFT_queueFastVLine(x1+w-1,y1,h, color);
  TFT_queueFastHLine(x1,y1+h-1,w, color);
  TFT_queueFastVLine(x1,y1,h, color);
  if (queued) send_data_finish();
}

// Draw a triangle
//------------------------------------------------------------------------------
static void TFT_drawTriangle(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1,
			     uint16_t x2, uint16_t y2, uint16_t color)
{
  TFT_drawLine(x0, y0, x1, y1, color);
  TFT_drawLine(x1, y1, x2, y2, color);
  TFT_drawLine(x2, y2, x0, y0, color);
}

// Fill a triangle
//-----------------------------------------------------------------------
static void TFT_fillTriangle(uint16_t x0, uint16_t y0,
				uint16_t x1, uint16_t y1,
				uint16_t x2, uint16_t y2, uint16_t color)
{
  int16_t a, b, y, last;

  // Sort coordinates by Y order (y2 >= y1 >= y0)
  if (y0 > y1) {
    swap(y0, y1); swap(x0, x1);
  }
  if (y1 > y2) {
    swap(y2, y1); swap(x2, x1);
  }
  if (y0 > y1) {
    swap(y0, y1); swap(x0, x1);
  }

  if(y0 == y2) { // Handle awkward all-on-same-line case as its own thing
    a = b = x0;
    if(x1 < a)      a = x1;
    else if(x1 > b) b = x1;
    if(x2 < a)      a = x2;
    else if(x2 > b) b = x2;
    TFT_drawFastHLine(a, y0, b-a+1, color);
    return;
  }

  int16_t
    dx01 = x1 - x0,
    dy01 = y1 - y0,
    dx02 = x2 - x0,
    dy02 = y2 - y0,
    dx12 = x2 - x1,
    dy12 = y2 - y1;
  int32_t
    sa   = 0,
    sb   = 0;

  // For upper part of triangle, find scanline crossings for segments
  // 0-1 and 0-2.  If y1=y2 (flat-bottomed triangle), the scanline y1
  // is included here (and second loop will be skipped, avoiding a /0
  // error there), otherwise scanline y1 is skipped here and handled
  // in the second loop...which also avoids a /0 error here if y0=y1
  // (flat-topped triangle).
  if(y1 == y2) last = y1;   // Include y1 scanline
  else         last = y1-1; // Skip it

  for(y=y0; y<=last; y++) {
    a   = x0 + sa / dy01;
    b   = x0 + sb / dy02;
    sa += dx01;
    sb += dx02;
    /* longhand:
    a = x0 + (x1 - x0) * (y - y0) / (y1 - y0);
    b = x0 + (x2 - x0) * (y - y0) / (y2 - y0);
    */
    if(a > b) swap(a,b);
    TFT_queueFastHLine(a, y, b-a+1, color);
  }

  // For lower part of triangle, find scanline crossings for segments
  // 0-2 and 1-2.  This loop is skipped if y1=y2.
  sa = dx12 * (y - y1);
  sb = dx02 * (y - y0);
  for(; y<=y2; y++) {
    a   = x1 + sa / dy12;
    b   = x0 + sb / dy02;
    sa += dx12;
    sb += dx02;
    /* longhand:
    a = x1 + (x2 - x1) * (y - y1) / (y2 - y1);
    b = x0 + (x2 - x0) * (y - y0) / (y2 - y0);
    */
    if(a > b) swap(a,b);
    TFT_queueFastHLine(a, y, b-a+1, color);
  }

  if (queued) send_data_finish();
}

//----------------------------------------------------------------------------
static void TFT_drawCircle(int16_t x, int16_t y, int radius, uint16_t color) {
  int f = 1 - radius;
  int ddF_x = 1;
  int ddF_y = -2 * radius;
  int x1 = 0;
  int y1 = radius;

  TFT_queuePixel(x, y + radius, color);
  TFT_queuePixel(x, y - radius, color);
  TFT_queuePixel(x + radius, y, color);
  TFT_queuePixel(x - radius, y, color);
  while(x1 < y1) {
    if (f >= 0) {
      y1--;
      ddF_y += 2;
      f += ddF_y;
    }
    x1++;
    ddF_x += 2;
    f += ddF_x;
    TFT_queuePixel(x + x1, y + y1, color);
    TFT_queuePixel(x - x1, y + y1, color);
    TFT_queuePixel(x + x1, y - y1, color);
    TFT_queuePixel(x - x1, y - y1, color);
    TFT_queuePixel(x + y1, y + x1, color);
    TFT_queuePixel(x - y1, y + x1, color);
    TFT_queuePixel(x + y1, y - x1, color);
    TFT_queuePixel(x - y1, y - x1, color);
  }
  if (queued) send_data_finish();
}

//----------------------------------------------------------------------------
static void TFT_fillCircle(int16_t x, int16_t y, int radius, uint16_t color) {
  int x1,y1;

  for (y1=-radius; y1<=0; y1++) {
    for (x1=-radius; x1<=0; x1++) {
      if (x1*x1+y1*y1 <= radius*radius) {
        TFT_queueFastHLine(x+x1, y+y1, 2*(-x1), color);
        TFT_queueFastHLine(x+x1, y-y1, 2*(-x1), color);
        break;
      }
    }
  }
  if (queued) send_data_finish();
}

// ================ Font and string functions ==================================

// return max width of the proportional font
//--------------------------------
static uint8_t getMaxWidth(void) {
  uint16_t tempPtr = 4; // point at first char data
  uint8_t cc,cw,ch,w = 0;
  do
  {
    cc = cfont.font[tempPtr++];
    tempPtr++;
    cw = cfont.font[tempPtr++];
    ch = cfont.font[tempPtr++];
    tempPtr += 2;
    if (cc != 0xFF) {
      if (cw != 0) {
        if (cw > w) w = cw;
        // packed bits
        tempPtr += (((cw * ch)-1) / 8) + 1;
      }
    }
  } while (cc != 0xFF);

  return w;
}

//--------------------------------------------------------
static int load_file_font(const char * fontfile, int info)
{
	if (userfont != NULL) {
		free(userfont);
		userfont = NULL;
	}

    struct stat sb;

    // Open the file
    FILE *fhndl = fopen(fontfile, "r");
    if (!fhndl) {
		printf("Error opening font file '%s'\r\n", fontfile);
		return 0;
    }

	// Get file size
    if (stat(fontfile, &sb) != 0) {
		printf("Error getting font file size\r\n");
		return 0;
    }
	int fsize = sb.st_size;
	if (fsize < 30) {
		printf("Error getting font file size\r\n");
		return 0;
	}

	userfont = malloc(fsize+4);
	if (userfont == NULL) {
		printf("Font memory allocation error\r\n");
		fclose(fhndl);
		return 0;
	}

	int read = fread(userfont, 1, fsize, fhndl);

	fclose(fhndl);

	if (read != fsize) {
		printf("Font read error\r\n");
		free(userfont);
		userfont = NULL;
		return 0;
	}

	userfont[read] = 0;
	if (strstr((char *)(userfont+read-8), "RPH_font") == NULL) {
		printf("Font ID not found\r\n");
		free(userfont);
		userfont = NULL;
		return 0;
	}

	// Check size
	int size = 0;
	int numchar = 0;
	int width = userfont[0];
	int height = userfont[1];
	uint8_t first = 255;
	uint8_t last = 0;
	//int offst = 0;
	int pminwidth = 255;
	int pmaxwidth = 0;

	if (width != 0) {
		// Fixed font
		numchar = userfont[3];
		first = userfont[2];
		last = first + numchar - 1;
		size = ((width * height * numchar) / 8) + 4;
	}
	else {
		// Proportional font
		size = 4; // point at first char data
		uint8_t charCode;
		int charwidth;

		do {
		    charCode = userfont[size];
		    charwidth = userfont[size+2];

		    if (charCode != 0xFF) {
		    	numchar++;
		    	if (charwidth != 0) size += ((((charwidth * userfont[size+3])-1) / 8) + 7);
		    	else size += 6;

		    	if (info) {
	    			if (charwidth > pmaxwidth) pmaxwidth = charwidth;
	    			if (charwidth < pminwidth) pminwidth = charwidth;
	    			if (charCode < first) first = charCode;
	    			if (charCode > last) last = charCode;
	    		}
		    }
		    else size++;
		  } while ((size < (read-8)) && (charCode != 0xFF));
	}

	if (size != (read-8)) {
		printf("Font size error: found %d expected %d\r\n)", size, (read-8));
		free(userfont);
		userfont = NULL;
		return 0;
	}

	if (info) {
		if (width != 0) {
			printf("Fixed width font:\n");
			printf("size: %d  width: %d  height: %d  characters: %d (%d~%d)\n", size, width, height, numchar, first, last);
		}
		else {
			printf("Proportional font:\n");
			printf("size: %d  width: %d~%d  height: %d  characters: %d (%d~%d)\n", size, pminwidth, pmaxwidth, height, numchar, first, last);
		}
	}
	return 1;
}

//----------------------------------------------------------
static void TFT_setFont(uint8_t font, const char *font_file)
{
  cfont.font = NULL;

  if (font == FONT_7SEG) {
    cfont.bitmap = 2;
    cfont.x_size = 24;
    cfont.y_size = 6;
    cfont.offset = 0;
    cfont.color  = _fg;
  }
  else {
	  if (font == USER_FONT) {
		  if (load_file_font(font_file, 0) == 0) cfont.font = tft_DefaultFont;
		  else cfont.font = userfont;
	  }
	  else cfont.font = tft_DefaultFont;

	  cfont.bitmap = 1;
	  cfont.x_size = cfont.font[0];
	  cfont.y_size = cfont.font[1];
	  cfont.offset = cfont.font[2];
	  if (cfont.x_size != 0) cfont.numchars = cfont.font[3];
	  else cfont.numchars = getMaxWidth();
  }
}

// private method to return the Glyph data for an individual character in the proportional font
//--------------------------------
static int getCharPtr(uint8_t c) {
  uint16_t tempPtr = 4; // point at first char data

  do {
    fontChar.charCode = cfont.font[tempPtr++];
    fontChar.adjYOffset = cfont.font[tempPtr++];
    fontChar.width = cfont.font[tempPtr++];
    fontChar.height = cfont.font[tempPtr++];
    fontChar.xOffset = cfont.font[tempPtr++];
    fontChar.xOffset = fontChar.xOffset < 0x80 ? fontChar.xOffset : (0x100 - fontChar.xOffset);
    fontChar.xDelta = cfont.font[tempPtr++];

    if (c != fontChar.charCode && fontChar.charCode != 0xFF) {
      if (fontChar.width != 0) {
        // packed bits
        tempPtr += (((fontChar.width * fontChar.height)-1) / 8) + 1;
      }
    }
  } while (c != fontChar.charCode && fontChar.charCode != 0xFF);

  fontChar.dataPtr = tempPtr;
  if (c == fontChar.charCode) {
    if (_forceFixed > 0) {
      // fix width & offset for forced fixed width
      fontChar.xDelta = cfont.numchars;
      fontChar.xOffset = (fontChar.xDelta - fontChar.width) / 2;
    }
  }

  if (fontChar.charCode != 0xFF) return 1;
  else return 0;
}

// print rotated proportional character
// character is already in fontChar
//--------------------------------------------------------------
static int rotatePropChar(int x, int y, int offset) {
  uint8_t ch = 0;
  double radian = rotation * 0.0175;
  float cos_radian = cos(radian);
  float sin_radian = sin(radian);

  uint8_t mask = 0x80;
  for (int j=0; j < fontChar.height; j++) {
    for (int i=0; i < fontChar.width; i++) {
      if (((i + (j*fontChar.width)) % 8) == 0) {
        mask = 0x80;
        ch = cfont.font[fontChar.dataPtr++];
      }

      int newX = (int)(x + (((offset + i) * cos_radian) - ((j+fontChar.adjYOffset)*sin_radian)));
      int newY = (int)(y + (((j+fontChar.adjYOffset) * cos_radian) + ((offset + i) * sin_radian)));

      if ((ch & mask) != 0) TFT_queuePixel(newX,newY,_fg);
      else if (!_transparent) TFT_queuePixel(newX,newY,_bg);

      mask >>= 1;
    }
  }
  if (queued) send_data_finish();

  return fontChar.xDelta+1;
}

// print non-rotated proportional character
// character is already in fontChar
//---------------------------------------------------------
static int printProportionalChar(int x, int y) {
  uint8_t i,j,ch=0;
  uint16_t cx,cy;

  // fill background if not transparent background
  if (!_transparent) {
    TFT_fillRect(x, y, fontChar.xDelta+1, cfont.y_size, _bg);
  }

  // draw Glyph
  uint8_t mask = 0x80;
  for (j=0; j < fontChar.height; j++) {
    for (i=0; i < fontChar.width; i++) {
      if (((i + (j*fontChar.width)) % 8) == 0) {
        mask = 0x80;
        ch = cfont.font[fontChar.dataPtr++];
      }

      if ((ch & mask) !=0) {
        cx = (uint16_t)(x+fontChar.xOffset+i);
        cy = (uint16_t)(y+j+fontChar.adjYOffset);
        TFT_queuePixel(cx, cy, _fg);
      }
      mask >>= 1;
    }
  }
  if (queued) send_data_finish();

  return fontChar.xDelta;
}

// non-rotated fixed width character
//----------------------------------------------
static void printChar(uint8_t c, int x, int y) {
  uint8_t i,j,ch,fz,mask;
  uint16_t k,temp,cx,cy;

  // fz = bytes per char row
  fz = cfont.x_size/8;
  if (cfont.x_size % 8) fz++;

  // get char address
  temp = ((c-cfont.offset)*((fz)*cfont.y_size))+4;

  // fill background if not transparent background
  if (!_transparent) {
    TFT_fillRect(x, y, cfont.x_size, cfont.y_size, _bg);
  }

  for (j=0; j<cfont.y_size; j++) {
    for (k=0; k < fz; k++) {
      ch = cfont.font[temp+k];
      mask=0x80;
      for (i=0; i<8; i++) {
        if ((ch & mask) !=0) {
          cx = (uint16_t)(x+i+(k*8));
          cy = (uint16_t)(y+j);
          TFT_queuePixel(cx, cy, _fg);
        }
        mask >>= 1;
      }
    }
    temp += (fz);
  }
  if (queued) send_data_finish();
}

// rotated fixed width character
//--------------------------------------------------------
static void rotateChar(uint8_t c, int x, int y, int pos) {
  uint8_t i,j,ch,fz,mask;
  uint16_t temp;
  int newx,newy;
  double radian = rotation*0.0175;
  float cos_radian = cos(radian);
  float sin_radian = sin(radian);
  int zz;

  if( cfont.x_size < 8 ) fz = cfont.x_size;
  else fz = cfont.x_size/8;
  temp=((c-cfont.offset)*((fz)*cfont.y_size))+4;

  for (j=0; j<cfont.y_size; j++) {
    for (zz=0; zz<(fz); zz++) {
      ch = cfont.font[temp+zz];
      mask = 0x80;
      for (i=0; i<8; i++) {
        newx=(int)(x+(((i+(zz*8)+(pos*cfont.x_size))*cos_radian)-((j)*sin_radian)));
        newy=(int)(y+(((j)*cos_radian)+((i+(zz*8)+(pos*cfont.x_size))*sin_radian)));

        if ((ch & mask) != 0) TFT_queuePixel(newx,newy,_fg);
        else if (!_transparent) TFT_queuePixel(newx,newy,_bg);
        mask >>= 1;
      }
    }
    temp+=(fz);
  }
  if (queued) send_data_finish();
  // calculate x,y for the next char
  TFT_X = (int)(x + ((pos+1) * cfont.x_size * cos_radian));
  TFT_Y = (int)(y + ((pos+1) * cfont.x_size * sin_radian));
}

// returns the string width in pixels. Useful for positions strings on the screen.
//----------------------------------
static int getStringWidth(char* str) {

  // is it 7-segment font?
  if (cfont.bitmap == 2) return ((2 * (2 * cfont.y_size + 1)) + cfont.x_size) * strlen(str);

  // is it a fixed width font?
  if (cfont.x_size != 0) return strlen(str) * cfont.x_size;
  else {
    // calculate the string width
    char* tempStrptr = str;
    int strWidth = 0;
    while (*tempStrptr != 0) {
      if (getCharPtr(*tempStrptr++)) strWidth += (fontChar.xDelta + 1);
    }
    return strWidth;
  }
}

//==============================================================================
/**
 * bit-encoded bar position of all digits' bcd segments
 *
 *                   6
 * 		  +-----+
 * 		3 |  .	| 2
 * 		  +--5--+
 * 		1 |  .	| 0
 * 		  +--.--+
 * 		     4
 */
static const uint16_t font_bcd[] = {
  0x200, // 0010 0000 0000  // -
  0x080, // 0000 1000 0000  // .
  0x06C, // 0100 0110 1100  // /, degree
  0x05f, // 0000 0101 1111, // 0
  0x005, // 0000 0000 0101, // 1
  0x076, // 0000 0111 0110, // 2
  0x075, // 0000 0111 0101, // 3
  0x02d, // 0000 0010 1101, // 4
  0x079, // 0000 0111 1001, // 5
  0x07b, // 0000 0111 1011, // 6
  0x045, // 0000 0100 0101, // 7
  0x07f, // 0000 0111 1111, // 8
  0x07d, // 0000 0111 1101  // 9
  0x900  // 1001 0000 0000  // :
};

//-------------------------------------------------------------------------------
static void barVert(int16_t x, int16_t y, int16_t w, int16_t l, uint16_t color) {
  TFT_fillTriangle(x+1, y+2*w, x+w, y+w+1, x+2*w-1, y+2*w, color);
  TFT_fillTriangle(x+1, y+2*w+l+1, x+w, y+3*w+l, x+2*w-1, y+2*w+l+1, color);
  TFT_fillRect(x, y+2*w+1, 2*w+1, l, color);
  if ((cfont.offset) && (color != _bg)) {
    TFT_drawTriangle(x+1, y+2*w, x+w, y+w+1, x+2*w-1, y+2*w, cfont.color);
    TFT_drawTriangle(x+1, y+2*w+l+1, x+w, y+3*w+l, x+2*w-1, y+2*w+l+1, cfont.color);
    TFT_drawRect(x, y+2*w+1, 2*w+1, l, cfont.color);
  }
}

//------------------------------------------------------------------------------
static void barHor(int16_t x, int16_t y, int16_t w, int16_t l, uint16_t color) {
  TFT_fillTriangle(x+2*w, y+2*w-1, x+w+1, y+w, x+2*w, y+1, color);
  TFT_fillTriangle(x+2*w+l+1, y+2*w-1, x+3*w+l, y+w, x+2*w+l+1, y+1, color);
  TFT_fillRect(x+2*w+1, y, l, 2*w+1, color);
  if ((cfont.offset) && (color != _bg)) {
    TFT_drawTriangle(x+2*w, y+2*w-1, x+w+1, y+w, x+2*w, y+1, cfont.color);
    TFT_drawTriangle(x+2*w+l+1, y+2*w-1, x+3*w+l, y+w, x+2*w+l+1, y+1, cfont.color);
    TFT_drawRect(x+2*w+1, y, l, 2*w+1, cfont.color);
  }
}

//------------------------------------------------------------------------------------------------
static void TFT_draw7seg(int16_t x, int16_t y, int8_t num, int16_t w, int16_t l, uint16_t color) {
  /* TODO: clipping */
  if (num < 0x2D || num > 0x3A) return;

  int16_t c = font_bcd[num-0x2D];
  int16_t d = 2*w+l+1;

  //if (!_transparent) TFT_fillRect(x, y, (2 * (2 * w + 1)) + l, (3 * (2 * w + 1)) + (2 * l), _bg);

  if (!(c & 0x001)) barVert(x+d, y+d, w, l, _bg);
  if (!(c & 0x002)) barVert(x,   y+d, w, l, _bg);
  if (!(c & 0x004)) barVert(x+d, y, w, l, _bg);
  if (!(c & 0x008)) barVert(x,   y, w, l, _bg);
  if (!(c & 0x010)) barHor(x, y+2*d, w, l, _bg);
  if (!(c & 0x020)) barHor(x, y+d, w, l, _bg);
  if (!(c & 0x040)) barHor(x, y, w, l, _bg);

  //if (!(c & 0x080)) TFT_fillRect(x+(d/2), y+2*d, 2*w+1, 2*w+1, _bg);
  if (!(c & 0x100)) TFT_fillRect(x+(d/2), y+d+2*w+1, 2*w+1, l/2, _bg);
  if (!(c & 0x800)) TFT_fillRect(x+(d/2), y+(2*w)+1+(l/2), 2*w+1, l/2, _bg);
  //if (!(c & 0x200)) TFT_fillRect(x+2*w+1, y+d, l, 2*w+1, _bg);

  if (c & 0x001) barVert(x+d, y+d, w, l, color);               // down right
  if (c & 0x002) barVert(x,   y+d, w, l, color);               // down left
  if (c & 0x004) barVert(x+d, y, w, l, color);                 // up right
  if (c & 0x008) barVert(x,   y, w, l, color);                 // up left
  if (c & 0x010) barHor(x, y+2*d, w, l, color);                // down
  if (c & 0x020) barHor(x, y+d, w, l, color);                  // middle
  if (c & 0x040) barHor(x, y, w, l, color);                    // up

  if (c & 0x080) {
    TFT_fillRect(x+(d/2), y+2*d, 2*w+1, 2*w+1, color);         // low point
    if (cfont.offset) TFT_drawRect(x+(d/2), y+2*d, 2*w+1, 2*w+1, cfont.color);
  }
  if (c & 0x100) {
    TFT_fillRect(x+(d/2), y+d+2*w+1, 2*w+1, l/2, color);       // down middle point
    if (cfont.offset) TFT_drawRect(x+(d/2), y+d+2*w+1, 2*w+1, l/2, cfont.color);
  }
  if (c & 0x800) {
    TFT_fillRect(x+(d/2), y+(2*w)+1+(l/2), 2*w+1, l/2, color); // up middle point
    if (cfont.offset) TFT_drawRect(x+(d/2), y+(2*w)+1+(l/2), 2*w+1, l/2, cfont.color);
  }
  if (c & 0x200) {
    TFT_fillRect(x+2*w+1, y+d, l, 2*w+1, color);               // middle, minus
    if (cfont.offset) TFT_drawRect(x+2*w+1, y+d, l, 2*w+1, cfont.color);
  }
}
//==============================================================================


//---------------------------------------------
static void TFT_print(char *st, int x, int y) {
  int stl, i, tmpw, tmph;
  uint8_t ch;

  if (cfont.bitmap == 0) return; // wrong font selected

  // for rotated string x cannot be RIGHT or CENTER
  if ((rotation != 0) && (x < -2)) return;

  stl = strlen(st); // number of characters in string to print

  // set CENTER or RIGHT possition
  tmpw = getStringWidth(st);
  if (x==RIGHT) x = dispWin.x2 - tmpw - 1;
  if (x==CENTER) x = (dispWin.x2 - tmpw - 1)/2;
  if (x < dispWin.x1) x = dispWin.x1;
  if (y < dispWin.y1) y = dispWin.y1;

  TFT_X = x;
  TFT_Y = y;
  int offset = TFT_OFFSET;

  tmph = cfont.y_size;
  // for non-proportional fonts, char width is the same for all chars
  if (cfont.x_size != 0) {
    if (cfont.bitmap == 2) { // 7-segment font
      tmpw = (2 * (2 * cfont.y_size + 1)) + cfont.x_size;        // character width
      tmph = (3 * (2 * cfont.y_size + 1)) + (2 * cfont.x_size);  // character height
    }
    else tmpw = cfont.x_size;
  }
  if ((TFT_Y + tmph - 1) > dispWin.y2) return;

  for (i=0; i<stl; i++) {
    ch = *st++; // get char

    if (cfont.x_size == 0) {
      // for proportional font get char width
      if (getCharPtr(ch)) tmpw = fontChar.xDelta;
    }

    if (ch == 0x0D) { // === '\r', erase to eol ====
      if ((!_transparent) && (rotation==0)) TFT_fillRect(TFT_X, TFT_Y,  dispWin.x2+1-TFT_X, tmph, _bg);
    }

    else if (ch == 0x0A) { // ==== '\n', new line ====
      if (cfont.bitmap == 1) {
        TFT_Y += tmph;
        if (TFT_Y > (dispWin.y2-tmph)) break;
        TFT_X = dispWin.x1;
      }
    }

    else { // ==== other characters ====
      // check if character can be displayed in the current line
      if ((TFT_X+tmpw) > (dispWin.x2+1)) {
        if (_wrap == 0) break;
        TFT_Y += tmph;
        if (TFT_Y > (dispWin.y2-tmph)) break;
        TFT_X = dispWin.x1;
      }

      // Let's print the character
      if (cfont.x_size == 0) {
        // == proportional font
        if (rotation==0) {
          TFT_X += printProportionalChar(TFT_X, TFT_Y)+1;
        }
        else {
          offset += rotatePropChar(x, y, offset);
          TFT_OFFSET = offset;
        }
      }
      // == fixed font
      else {
        if (cfont.bitmap == 1) {
          if ((ch < cfont.offset) || ((ch-cfont.offset) > cfont.numchars)) ch = cfont.offset;
          if (rotation==0) {
            printChar(ch, TFT_X, TFT_Y);
            TFT_X += tmpw;
          }
          else rotateChar(ch, x, y, i);
        }
        else if (cfont.bitmap == 2) { // 7-seg font
          TFT_draw7seg(TFT_X, TFT_Y, ch, cfont.y_size, cfont.x_size, _fg);
          TFT_X += (tmpw + 2);
        }
      }
    }
  }
}

//========================================
static int compile_font_file(lua_State* L)
{
	char outfile[128] = {'\0'};
	size_t len;
    struct stat sb;

	const char *fontfile = luaL_checklstring( L, 1, &len );

	// check here that filename end with ".c".
	if ((len < 3) || (len > 125) || (strcmp(fontfile + len - 2, ".c") != 0)) return luaL_error(L, "not a .c file");

	sprintf(outfile, "%s", fontfile);
	sprintf(outfile+strlen(outfile)-1, "fon");

	// Open the source file
    if (stat(fontfile, &sb) != 0) {
    	return luaL_error(L, "Error opening source file '%s'", fontfile);
    }
    // Open the file
    FILE *ffd = fopen(fontfile, "r");
    if (!ffd) {
    	return luaL_error(L, "Error opening source file '%s'", fontfile);
    }

	// Open the font file
    FILE *ffd_out= fopen(outfile, "w");
	if (!ffd_out) {
		fclose(ffd);
		return luaL_error(L, "error opening destination file");
	}

	// Get file size
	int fsize = sb.st_size;
	if (fsize <= 0) {
		fclose(ffd);
		fclose(ffd_out);
		return luaL_error(L, "source file size error");
	}

	char *sourcebuf = malloc(fsize+4);
	if (sourcebuf == NULL) {
		fclose(ffd);
		fclose(ffd_out);
		return luaL_error(L, "memory allocation error");
	}
	char *fbuf = sourcebuf;

	int rdsize = fread(fbuf, 1, fsize, ffd);
	fclose(ffd);

	if (rdsize != fsize) {
		free(fbuf);
		fclose(ffd_out);
		return luaL_error(L, "error reading from source file");
	}

	*(fbuf+rdsize) = '\0';

	fbuf = strchr(fbuf, '{');			// beginning of font data
	char *fend = strstr(fbuf, "};");	// end of font data

	if ((fbuf == NULL) || (fend == NULL) || ((fend-fbuf) < 22)) {
		free(fbuf);
		fclose(ffd_out);
		return luaL_error(L, "wrong source file format");
	}

	fbuf++;
	*fend = '\0';
	char hexstr[5] = {'\0'};
	int lastline = 0;

	fbuf = strstr(fbuf, "0x");
	int size = 0;
	char *nextline;
	char *numptr;

	int bptr = 0;

	while ((fbuf != NULL) && (fbuf < fend) && (lastline == 0)) {
		nextline = strchr(fbuf, '\n'); // beginning of the next line
		if (nextline == NULL) {
			nextline = fend-1;
			lastline++;
		}
		else nextline++;

		while (fbuf < nextline) {
			numptr = strstr(fbuf, "0x");
			if ((numptr == NULL) || ((fbuf+4) > nextline)) numptr = strstr(fbuf, "0X");
			if ((numptr != NULL) && ((numptr+4) <= nextline)) {
				fbuf = numptr;
				if (bptr >= 128) {
					// buffer full, write to file
                    if (fwrite(outfile, 1, 128, ffd_out) != 128) goto error;
					bptr = 0;
					size += 128;
				}
				memcpy(hexstr, fbuf, 4);
				hexstr[4] = 0;
				outfile[bptr++] = (uint8_t)strtol(hexstr, NULL, 0);
				fbuf += 4;
			}
			else fbuf = nextline;
		}
		fbuf = nextline;
	}

	if (bptr > 0) {
		size += bptr;
        if (fwrite(outfile, 1, bptr, ffd_out) != bptr) goto error;
	}

	// write font ID
	sprintf(outfile, "RPH_font");
    if (fwrite(outfile, 1, 8, ffd_out) != 8) goto error;

	fclose(ffd_out);

	free(sourcebuf);

	// === Test compiled font ===
	sprintf(outfile, "%s", fontfile);
	sprintf(outfile+strlen(outfile)-1, "fon");

	uint8_t *uf = userfont; // save userfont pointer
	userfont = NULL;
	if (load_file_font(outfile, 1) == 0) printf("Error compiling file!\n");
	else {
		free(userfont);
		printf("File compiled successfully.\n");
	}
	userfont = uf; // restore userfont

	return 0;

error:
	fclose(ffd_out);
	free(sourcebuf);
	return luaL_error(L, "error writing to destination file");
}


// ================ Service functions ==========================================

// Change the screen rotation.
// Input: m new rotation value (0 to 3)
//--------------------------------------
static void TFT_setRotation(uint8_t m) {
  uint8_t rotation = m & 3; // can't be higher than 3
  uint8_t send = 1;
  uint8_t madctl = 0;

  if (m > 3) madctl = (m & 0xF8); // for testing, manually set MADCTL register
  else {
	  orientation = m;
	  if (TFT_type == 0) {
		if ((rotation & 1)) {
			_width  = ST7735_HEIGHT;
			_height = ST7735_WIDTH;
		}
		else {
			_width  = ST7735_WIDTH;
			_height = ST7735_HEIGHT;
		}
		switch (rotation) {
		  case PORTRAIT:
			madctl = (MADCTL_MX | MADCTL_MY | MADCTL_RGB);
			break;
		  case LANDSCAPE:
			madctl = (MADCTL_MY | MADCTL_MV | MADCTL_RGB);
			break;
		  case PORTRAIT_FLIP:
			madctl = (MADCTL_RGB);
			break;
		  case LANDSCAPE_FLIP:
			madctl = (MADCTL_MX | MADCTL_MV | MADCTL_RGB);
			break;
		}
	  }
	  else if (TFT_type == 1) {
		if ((rotation & 1)) {
			_width  = ILI9341_HEIGHT;
			_height = ILI9341_WIDTH;
		}
		else {
			_width  = ILI9341_WIDTH;
			_height = ILI9341_HEIGHT;
		}
		switch (rotation) {
		  case PORTRAIT:
			madctl = (MADCTL_MX | MADCTL_BGR);
			break;
		  case LANDSCAPE:
			madctl = (MADCTL_MV | MADCTL_BGR);
			break;
		  case PORTRAIT_FLIP:
			madctl = (MADCTL_MY | MADCTL_BGR);
			break;
		  case LANDSCAPE_FLIP:
			madctl = (MADCTL_MX | MADCTL_MY | MADCTL_MV | MADCTL_BGR);
			break;
		}
	  }
	  /*
	  else if (TFT_type == 2) {
		_width  = XADOW_WIDTH;
		_height = XADOW_HEIGHT;
		switch (rotation) {
		  case PORTRAIT:
			madctl = 0;
			break;
		  case LANDSCAPE:
			madctl = (MADCTL_MV | MADCTL_MX);
			break;
		  case PORTRAIT_FLIP:
			madctl = 0;
			rotation = PORTRAIT;
			break;
		  case LANDSCAPE_FLIP:
			madctl = (MADCTL_MV | MADCTL_MX);
			rotation = LANDSCAPE;
			break;
		}
	  }
	  */
	  else send = 0;
  }

  if (send) {
	  tft_cmd(TFT_MADCTL);
	  tft_data(&madctl, 1);
  }

  dispWin.x1 = 0;
  dispWin.y1 = 0;
  dispWin.x2 = _width-1;
  dispWin.y2 = _height-1;

}

// Send the command to invert all of the colors.
// Input: i 0 to disable inversion; non-zero to enable inversion
//-------------------------------------------------
static void TFT_invertDisplay(const uint8_t mode) {
  if ( mode == INVERT_ON ) tft_cmd(TFT_INVONN);
  else tft_cmd(TFT_INVOFF);
}

//--------------------------------------------------
static uint8_t checkParam(uint8_t n, lua_State* L) {
  if (lua_gettop(L) < n) {
    printf("not enough parameters\r\n" );
    return 1;
  }
  return 0;
}

/**
 * Converts the components of a color, as specified by the HSB
 * model, to an equivalent set of values for the default RGB model.
 * The _sat and _brightnesscomponents
 * should be floating-point values between zero and one (numbers in the range 0.0-1.0)
 * The _hue component can be any floating-point number.  The floor of this number is
 * subtracted from it to create a fraction between 0 and 1.
 * This fractional number is then multiplied by 360 to produce the hue
 * angle in the HSB color model.
 * The integer that is returned by HSBtoRGB encodes the
 * value of a color in bits 0-15 of an integer value
*/
//-------------------------------------------------------------------
static uint16_t HSBtoRGB(float _hue, float _sat, float _brightness) {
 float red = 0.0;
 float green = 0.0;
 float blue = 0.0;

 if (_sat == 0.0) {
   red = _brightness;
   green = _brightness;
   blue = _brightness;
 } else {
   if (_hue == 360.0) {
     _hue = 0;
   }

   int slice = (int)(_hue / 60.0);
   float hue_frac = (_hue / 60.0) - slice;

   float aa = _brightness * (1.0 - _sat);
   float bb = _brightness * (1.0 - _sat * hue_frac);
   float cc = _brightness * (1.0 - _sat * (1.0 - hue_frac));

   switch(slice) {
     case 0:
         red = _brightness;
         green = cc;
         blue = aa;
         break;
     case 1:
         red = bb;
         green = _brightness;
         blue = aa;
         break;
     case 2:
         red = aa;
         green = _brightness;
         blue = cc;
         break;
     case 3:
         red = aa;
         green = bb;
         blue = _brightness;
         break;
     case 4:
         red = cc;
         green = aa;
         blue = _brightness;
         break;
     case 5:
         red = _brightness;
         green = aa;
         blue = bb;
         break;
     default:
         red = 0.0;
         green = 0.0;
         blue = 0.0;
         break;
   }
 }

 uint8_t ired = (uint8_t)(red * 31.0);
 uint8_t igreen = (uint8_t)(green * 63.0);
 uint8_t iblue = (uint8_t)(blue * 31.0);

 return (uint16_t)((ired << 11) | (igreen << 5) | (iblue & 0x001F));
}

//-------------------------------------------------
static uint16_t getColor(lua_State* L, uint8_t n) {
  if( lua_istable( L, n ) ) {
    uint8_t i;
    uint8_t cl[3];
    uint8_t datalen = lua_rawlen( L, n );
    if (datalen < 3) return _fg;

    for( i = 0; i < 3; i++ )
    {
      lua_rawgeti( L, n, i + 1 );
      cl[i] = ( int )luaL_checkinteger( L, -1 );
      lua_pop( L, 1 );
    }
    if (cl[0] > 0x1F) cl[0] = 0x1F;
    if (cl[1] > 0x3F) cl[1] = 0x3F;
    if (cl[2] > 0x1F) cl[2] = 0x1F;
    return (cl[0] << 11) | (cl[1] << 5) | cl[2];
  }
  else {
    return luaL_checkinteger( L, n );
  }
}

//--------------------------
static void _initvar(void) {
  rotation = 0;
  _wrap = 0;
  _transparent = 0;
  _forceFixed = 0;
  dispWin.x2 = _width-1;
  dispWin.y2 = _height-1;
  dispWin.x1 = 0;
  dispWin.y1 = 0;
}



// ================ JPG SUPPORT ================================================
// User defined device identifier
typedef struct {
	FILE *fhndl;	// File handler for input function
    uint16_t x;		// image top left point X position
    uint16_t y;		// image top left point Y position
} JPGIODEV;


// User defined call-back function to input JPEG data
//---------------------
static UINT tjd_input (
	JDEC* jd,		// Decompression object
	BYTE* buff,		// Pointer to the read buffer (NULL:skip)
	UINT nd			// Number of bytes to read/skip from input stream
)
{
	int rb = 0;
	// Device identifier for the session (5th argument of jd_prepare function)
	JPGIODEV *dev = (JPGIODEV*)jd->device;

	if (buff) {	// Read nd bytes from the input strem
		rb = fread(buff, 1, nd, dev->fhndl);
		return rb;	// Returns actual number of bytes read
	}
	else {	// Remove nd bytes from the input stream
		if (fseek(dev->fhndl, nd, SEEK_CUR) >= 0) return nd;
		else return 0;
	}
}

// User defined call-back function to output RGB bitmap
//----------------------
static UINT tjd_output (
	JDEC* jd,		// Decompression object of current session
	void* bitmap,	// Bitmap data to be output
	JRECT* rect		// Rectangular region to output
)
{
	// Device identifier for the session (5th argument of jd_prepare function)
	JPGIODEV *dev = (JPGIODEV*)jd->device;

	// ** Put the rectangular into the display device **
	uint16_t x;
	uint16_t y;
	BYTE *src = (BYTE*)bitmap;
	uint16_t left = rect->left + dev->x;
	uint16_t top = rect->top + dev->y;
	uint16_t right = rect->right + dev->x;
	uint16_t bottom = rect->bottom + dev->y;

	if ((left >= _width) || (top >= _height)) return 1;	// out of screen area, return

	int len = ((right-left+1) * (bottom-top+1))*2;		// calculate length of data

	if ((len > 0) && (len <= (TFT_LINEBUF_MAX_SIZE))) {
		uint16_t bufidx = 0;

	    for (y = top; y <= bottom; y++) {
		    for (x = left; x <= right; x++) {
		    	// Clip to display area
		    	if ((x < _width) && (y < _height)) {
		    		tft_line[bufidx++] = *src++;
		    		tft_line[bufidx++] = *src++;
		    	}
		    	else src += 2;
		    }
	    }
	    if (right > _width) right = _width-1;
	    if (bottom > _height) bottom = _height-1;
		//printf("output: %d [%d] (%d,%d,%d,%d)\r\n", len, bufidx/2, left,top,right,bottom);
	    if (queued) send_data_finish();
	    send_data(left, top, right, bottom, bufidx/2, tft_line);
	}
	else {
		printf("max data size exceded: %d (%d,%d,%d,%d)\r\n", len, left,top,right,bottom);
		return 0;  // stop decompression
	}

	return 1;	// Continue to decompression
}

//=======================================
static int ltft_jpg_image( lua_State* L )
{
	const char *fname;
	size_t len;
	JPGIODEV dev;
    char *basename;
    struct stat sb;

	int start = clock();

	int x = luaL_checkinteger( L, 1 );
	int y = luaL_checkinteger( L, 2 );
	int maxscale = luaL_checkinteger( L, 3 );
	if ((maxscale < 0) || (maxscale > 3)) maxscale = 3;

	fname = luaL_checklstring( L, 4, &len );

    if (strlen(fname) == 0) return 0;

    basename = strrchr(fname, '/');
    if (basename == NULL) basename = (char *)fname;
    else basename++;
    if (strlen(basename) == 0) return 0;

    if (stat(fname, &sb) != 0) {
        return luaL_error(L, strerror(errno));
    }

    dev.fhndl = fopen(fname, "r");
    if (!dev.fhndl) {
        return luaL_error(L, strerror(errno));
    }

	char *work;				// Pointer to the working buffer (must be 4-byte aligned)
	UINT sz_work = 3800;	// Size of the working buffer (must be power of 2)
	JDEC jd;				// Decompression object (70 bytes)
	JRESULT rc;
	BYTE scale = 0;
	uint8_t radj = 0;
	uint8_t badj = 0;

	if ((x < 0) && (x != CENTER) && (x != RIGHT)) x = 0;
	if ((y < 0) && (y != CENTER) && (y != BOTTOM)) y = 0;
	if (x > (_width-5)) x = _width - 5;
	if (y > (_height-5)) y = _height - 5;

	work = malloc(sz_work);
	if (work) {
		rc = jd_prepare(&jd, tjd_input, (void *)work, sz_work, &dev);
		if (rc == JDR_OK) {
			if (x == CENTER) {
				x = _width - (jd.width >> scale);
				if (x < 0) {
					if (maxscale) {
						for (scale = 0; scale <= maxscale; scale++) {
							if (((jd.width >> scale) <= (_width)) && ((jd.height >> scale) <= (_height))) break;
							if (scale == maxscale) break;
						}
						x = _width - (jd.width >> scale);
						if (x < 0) x = 0;
						else x >>= 1;
						maxscale = 0;
					}
					else x = 0;
				}
				else x >>= 1;
			}
			if (y == CENTER) {
				y = _height - (jd.height >> scale);
				if (y < 0) {
					if (maxscale) {
						for (scale = 0; scale <= maxscale; scale++) {
							if (((jd.width >> scale) <= (_width)) && ((jd.height >> scale) <= (_height))) break;
							if (scale == maxscale) break;
						}
						y = _height - (jd.height >> scale);
						if (y < 0) y = 0;
						else y >>= 1;
						maxscale = 0;
					}
					else y = 0;
				}
				else y >>= 1;
			}
			if (x == RIGHT) {
				x = 0;
				radj = 1;
			}
			if (y == BOTTOM) {
				y = 0;
				badj = 1;
			}
			// Determine scale factor
			if (maxscale) {
				for (scale = 0; scale <= maxscale; scale++) {
					if (((jd.width >> scale) <= (_width-x)) && ((jd.height >> scale) <= (_height-y))) break;
					if (scale == maxscale) break;
				}
			}
			//printf("Image dimensions: %dx%d, scale: %d, bytes used: %d\r\n", jd.width, jd.height, scale, jd.sz_pool);

			if (radj) {
				x = _width - (jd.width >> scale);
				if (x < 0) x = 0;
			}
			if (badj) {
				y = _height - (jd.height >> scale);
				if (y < 0) y = 0;
			}
			dev.x = x;
			dev.y = y;
			// Start to decompress the JPEG file
			rc = jd_decomp(&jd, tjd_output, scale);
		    if (queued) send_data_finish();
			if (rc != JDR_OK) {
				printf("jpg decompression error %d\r\n", rc);
			}
		}
		else {
			printf("jpg prepare error %d\r\n", rc);
		}

		free(work);  // free work buffer
	}
	else {
		printf("work buffer allocation error\r\n");
	}

	fclose(dev.fhndl);  // close input file

	lua_pushinteger(L, clock() - start);
	return 1;
}

//==================================
static int tft_image( lua_State* L )
{
  if (checkParam(5, L)) return 0;

  const char *fname;
  char *basename;
  FILE *fhndl = 0;
  struct stat sb;
  uint32_t xrd = 0;
  size_t len;

  int x = luaL_checkinteger( L, 1 );
  int y = luaL_checkinteger( L, 2 );
  int xsize = luaL_checkinteger( L, 3 );
  int ysize = luaL_checkinteger( L, 4 );
  fname = luaL_checklstring( L, 5, &len );

  if (strlen(fname) == 0) return 0;

  if (x == CENTER) x = (_width - xsize) / 2;
  else if (x == RIGHT) x = (_width - xsize);
  if (x < 0) x = 0;

  if (y == CENTER) y = (_height - ysize) / 2;
  else if (y == BOTTOM) y = (_height - ysize);
  if (y < 0) y = 0;

  // crop to disply width
  int xend;
  if ((x+xsize) > _width) xend = _width-1;
  else xend = x+xsize-1;
  int disp_xsize = xend-x+1;
  if ((disp_xsize <= 1) || (y >= _height)) {
	  printf("image out of screen.\r\n");
	  return 0;
  }

  basename = strrchr(fname, '/');
  if (basename == NULL) basename = (char *)fname;
  else basename++;
  if (strlen(basename) == 0) return 0;

  if (stat(fname, &sb) != 0) {
      return luaL_error(L, strerror(errno));
  }

  fhndl = fopen(fname, "r");
  if (!fhndl) {
      return luaL_error(L, strerror(errno));
  }

  disp_xsize *= 2;
  do { // read 1 image line from file and send to display
	xrd = fread(tft_line, 1, 2*xsize, fhndl);  // read line from file
	if (xrd != (2*xsize)) {
		printf("Error reading line: %d\r\n", xrd);
		break;
	}

    if (queued) send_data_finish();
    send_data(x, y, xend, y, xsize, tft_line);

	y++;
	if (y >= _height) break;
	ysize--;

  } while (ysize > 0);
  if (queued) send_data_finish();

  fclose(fhndl);

  return 0;
}

//=====================================
static int tft_bmpimage( lua_State* L )
{
	if (checkParam(3, L)) return 0;

	const char *fname;
	char *basename;
	FILE *fhndl = 0;
	struct stat sb;
	uint8_t buf[960];  // max bytes per line = 3 * 320
	uint32_t xrd = 0;
	size_t len;

	int x = luaL_checkinteger( L, 1 );
	int y = luaL_checkinteger( L, 2 );
	fname = luaL_checklstring( L, 3, &len );

	if (strlen(fname) == 0) return 0;

	basename = strrchr(fname, '/');
	if (basename == NULL) basename = (char *)fname;
	else basename++;
	if (strlen(basename) == 0) return 0;

	if (stat(fname, &sb) != 0) {
		return luaL_error(L, strerror(errno));
	}

	fhndl = fopen(fname, "r");
	if (!fhndl) {
		return luaL_error(L, strerror(errno));
	}

	xrd = fread(buf, 1, 54, fhndl);  // read header
	if (xrd != 54) {
exithd:
		fclose(fhndl);
		printf("Error reading header\r\n");
		return 0;
	}

	uint16_t wtemp;
	uint32_t temp;
	uint32_t offset;
	uint32_t xsize;
	uint32_t ysize;

	// Check image header
	if ((buf[0] != 'B') || (buf[1] != 'M')) goto exithd;

	memcpy(&offset, buf+10, 4);
	memcpy(&temp, buf+14, 4);
	if (temp != 40) goto exithd;
	memcpy(&wtemp, buf+26, 2);
	if (wtemp != 1) goto exithd;
	memcpy(&wtemp, buf+28, 2);
	if (wtemp != 24) goto exithd;
	memcpy(&temp, buf+30, 4);
	if (temp != 0) goto exithd;

	memcpy(&xsize, buf+18, 4);
	memcpy(&ysize, buf+22, 4);

	// Adjust position
	if (x == CENTER) x = (_width - xsize) / 2;
	else if (x == RIGHT) x = (_width - xsize);
	if (x < 0) x = 0;

	if (y == CENTER) y = (_height - ysize) / 2;
	else if (y == BOTTOM) y = (_height - ysize);
	if (y < 0) y = 0;

	// Crop to display width
	int xend;
	if ((x+xsize) > _width) xend = _width-1;
	else xend = x+xsize-1;
	int disp_xsize = xend-x+1;
	if ((disp_xsize <= 1) || (y >= _height)) {
		printf("image out of screen.\r\n");
		goto exit;
	}

	int i,j;
	while (ysize > 0) {
		// Position at line start
		// ** BMP images are stored in file from LAST to FIRST line
		//    so we have to read from the end line first

		if (fseek(fhndl, offset+((ysize-1)*(xsize*3)), SEEK_SET) != 0) break;

		// ** read one image line from file and send to display **
		// read only the part of image line which can be shown on screen
		xrd = fread(buf, 1, disp_xsize*3, fhndl);  // read line from file
		if (xrd != (disp_xsize*3)) {
			printf("Error reading line: %d (%d)\r\n", y, xrd);
			break;
		}
		//printf("read line: %d %d %d %d %d\n", y, offset, xrd, xsize, xend);

		j = 0;
		for (i=0;i < xrd;i += 3) {
			// get RGB888 and convert to RGB565
			// BMP BYTES ORDER: B8-G8-R8 !!
			tft_line[j]    = buf[i+2] & 0xF8;			// R5
			tft_line[j]   |= buf[i+1] >> 5;				// G6 Hi
			tft_line[j+1]  = (buf[i+1] << 3) & 0xE0;	// G6 Lo
			tft_line[j+1] |= buf[i] >> 3;				// B5
			j += 2;
		}
		//printf("npixel: %d\n", disp_xsize);
	    if (queued) send_data_finish();
	    send_data(x, y, xend, y, disp_xsize, tft_line);

		y++;	// next image line
		if (y >= _height) break;
		ysize--;
	}
    if (queued) send_data_finish();

exit:
	fclose(fhndl);

	return 0;
}


//====================================
static int ltft_init( lua_State* L ) {

    uint8_t typ = luaL_checkinteger( L, 1);

    TFT_setFont(DEFAULT_FONT, NULL);
    _fg = TFT_GREEN;
    _bg = TFT_BLACK;

    if (typ < 3) TFT_type = 0;							// ST7735 type display
    else if ((typ == 3) || (typ == 4)) TFT_type = 1;	// ILI7341 type display
    else {
        return luaL_error( L, "unsupported display chipset" );
    }

    tft_spi_init(typ);

    typ = LANDSCAPE;
    if (lua_gettop(L) > 1) {
      typ = luaL_checkinteger( L, 2 ) % 4;
    }
    TFT_setRotation(typ);
    TFT_fillScreen(TFT_BLACK);
    _initvar();

	return 0;
}

//==================================
static int tft_gettype(lua_State *L)
{
    lua_pushinteger( L, TFT_type);
    return 1;
}

//=========================================
static int tft_set_brightness(lua_State *L)
{
    return 0;
}

//======================================
static int tft_setorient( lua_State* L )
{
  orientation = luaL_checkinteger( L, 1 );
  TFT_setRotation(orientation);
  TFT_fillScreen(_bg);

  return 0;
}

//==================================
static int tft_clear( lua_State* L )
{
	uint16_t color = TFT_BLACK;

	if (lua_gettop(L) > 0) color = getColor( L, 1 );

	int start = clock();
	_TFT_pushColorRep(0, 0, _width-1, _height-1, TFT_BLACK, (uint32_t)(_height*_width));

	lua_pushinteger(L, clock() - start);
	_bg = color;
	_initvar();

	return 1;
}

//===================================
static int tft_invert( lua_State* L )
{
  uint16_t inv = luaL_checkinteger( L, 1 );
  TFT_invertDisplay(inv);
  return 0;
}

//====================================
static int tft_setwrap( lua_State* L )
{
  _wrap = luaL_checkinteger( L, 1 );
  return 0;
}

//======================================
static int tft_settransp( lua_State* L )
{
  _transparent = luaL_checkinteger( L, 1 );
  return 0;
}

//===================================
static int tft_setrot( lua_State* L )
{
  rotation = luaL_checkinteger( L, 1 );
  return 0;
}

//===================================
static int tft_setfixed( lua_State* L )
{
  _forceFixed = luaL_checkinteger( L, 1 );
  return 0;
}

//====================================
static int tft_setfont( lua_State* L )
{
  if (checkParam(1, L)) return 0;

  uint8_t fnt = DEFAULT_FONT;
  size_t fnlen = 0;
  const char* fname = NULL;

  if (lua_type(L, 1) == LUA_TNUMBER) {
	  fnt = luaL_checkinteger( L, 1 );
  }
  else if (lua_type(L, 1) == LUA_TSTRING) {
	  fnt = USER_FONT;
	  fname = lua_tolstring(L, -1, &fnlen);
  }

  TFT_setFont(fnt, fname);

  if (fnt == FONT_7SEG) {
    if (lua_gettop(L) > 2) {
      uint8_t l = luaL_checkinteger( L, 2 );
      uint8_t w = luaL_checkinteger( L, 3 );
      if (l < 6) l = 6;
      if (l > 40) l = 40;
      if (w < 1) w = 1;
      if (w > (l/2)) w = l/2;
      if (w > 12) w = 12;
      cfont.x_size = l;
      cfont.y_size = w;
      cfont.offset = 0;
      cfont.color  = _fg;
      if (lua_gettop(L) > 3) {
        if (w > 1) {
          cfont.offset = 1;
          cfont.color  = getColor( L, 4 );
        }
      }
    }
    else {  // default size
      cfont.x_size = 12;
      cfont.y_size = 2;
    }
  }
  return 0;
}

//========================================
static int tft_getfontsize( lua_State* L )
{
  if (cfont.bitmap == 1) {
    if (cfont.x_size != 0) lua_pushinteger( L, cfont.x_size );
    else lua_pushinteger( L, getMaxWidth() );
    lua_pushinteger( L, cfont.y_size );
  }
  else if (cfont.bitmap == 2) {
    lua_pushinteger( L, (2 * (2 * cfont.y_size + 1)) + cfont.x_size );
    lua_pushinteger( L, (3 * (2 * cfont.y_size + 1)) + (2 * cfont.x_size) );
  }
  else {
    lua_pushinteger( L, 0);
    lua_pushinteger( L, 0);
  }
  return 2;
}

//==========================================
static int tft_getscreensize( lua_State* L )
{
  lua_pushinteger( L, _width);
  lua_pushinteger( L, _height);
  return 2;
}

//==========================================
static int tft_getfontheight( lua_State* L )
{
  if (cfont.bitmap == 1) {
	// Bitmap font
    lua_pushinteger( L, cfont.y_size );
  }
  else if (cfont.bitmap == 2) {
	// 7-segment font
    lua_pushinteger( L, (3 * (2 * cfont.y_size + 1)) + (2 * cfont.x_size) );
  }
  else {
    lua_pushinteger( L, 0);
  }
  return 1;
}

//===============================
static int tft_on( lua_State* L )
{
  tft_cmd(TFT_DISPON);
  return 0;
}

//================================
static int tft_off( lua_State* L )
{
  tft_cmd(TFT_DISPOFF);
  return 0;
}

//=====================================
static int tft_setcolor( lua_State* L )
{
  if (checkParam(1, L)) return 0;

  _fg = getColor( L, 1 );
  if (lua_gettop(L) > 1) _bg = getColor( L, 2 );
  return 0;
}

//=======================================
static int tft_setclipwin( lua_State* L )
{
  if (checkParam(4, L)) return 0;

  dispWin.x1 = luaL_checkinteger( L, 1 );
  dispWin.y1 = luaL_checkinteger( L, 2 );
  dispWin.x2 = luaL_checkinteger( L, 3 );
  dispWin.y2 = luaL_checkinteger( L, 4 );

  if (dispWin.x2 >= _width) dispWin.x2 = _width-1;
  if (dispWin.y2 >= _height) dispWin.y2 = _height-1;
  if (dispWin.x1 > dispWin.x2) dispWin.x1 = dispWin.x2;
  if (dispWin.y1 > dispWin.y2) dispWin.y1 = dispWin.y2;

  return 0;
}

//=========================================
static int tft_resetclipwin( lua_State* L )
{
  dispWin.x2 = _width-1;
  dispWin.y2 = _height-1;
  dispWin.x1 = 0;
  dispWin.y1 = 0;

  return 0;
}

//=====================================
static int tft_HSBtoRGB( lua_State* L )
{
  float hue = luaL_checknumber(L, 1);
  float sat = luaL_checknumber(L, 2);
  float bri = luaL_checknumber(L, 3);

  lua_pushinteger(L, HSBtoRGB(hue, sat, bri));

  return 1;
}

//=====================================
static int tft_putpixel( lua_State* L )
{
  if (checkParam(2, L)) return 0;

  uint16_t x = luaL_checkinteger( L, 1 );
  uint16_t y = luaL_checkinteger( L, 2 );
  uint16_t color = _fg;

  if (lua_gettop(L) > 2) color = getColor( L, 3 );

  TFT_drawPixel(x,y,color);

  return 0;
}

//=====================================
static int tft_drawline( lua_State* L )
{
  if (checkParam(4, L)) return 0;

  uint16_t color = _fg;
  if (lua_gettop(L) > 4) color = getColor( L, 5 );
  uint16_t x0 = luaL_checkinteger( L, 1 );
  uint16_t y0 = luaL_checkinteger( L, 2 );
  uint16_t x1 = luaL_checkinteger( L, 3 );
  uint16_t y1 = luaL_checkinteger( L, 4 );
  TFT_drawLine(x0,y0,x1,y1,color);
  return 0;
}

//=================================
static int tft_rect( lua_State* L )
{
  if (checkParam(5, L)) return 0;

  uint16_t fillcolor = _bg;
  if (lua_gettop(L) > 5) fillcolor = getColor( L, 6 );
  uint16_t x = luaL_checkinteger( L, 1 );
  uint16_t y = luaL_checkinteger( L, 2 );
  uint16_t w = luaL_checkinteger( L, 3 );
  uint16_t h = luaL_checkinteger( L, 4 );
  uint16_t color = getColor( L, 5 );
  if (lua_gettop(L) > 5) TFT_fillRect(x,y,w,h,fillcolor);
  if (fillcolor != color) TFT_drawRect(x,y,w,h,color);
  return 0;
}

//=================================
static int tft_circle( lua_State* L )
{
  if (checkParam(4, L)) return 0;

  uint16_t fillcolor = _bg;
  if (lua_gettop(L) > 4) fillcolor = getColor( L, 5 );
  uint16_t x = luaL_checkinteger( L, 1 );
  uint16_t y = luaL_checkinteger( L, 2 );
  uint16_t r = luaL_checkinteger( L, 3 );
  uint16_t color = getColor( L, 4 );
  if (lua_gettop(L) > 4) TFT_fillCircle(x,y,r,fillcolor);
  if (fillcolor != color) TFT_drawCircle(x,y,r,color);
  return 0;
}

//=====================================
static int tft_triangle( lua_State* L )
{
  if (checkParam(7, L)) return 0;

  uint16_t fillcolor = _bg;
  if (lua_gettop(L) > 7) fillcolor = getColor( L, 8 );
  uint16_t x0 = luaL_checkinteger( L, 1 );
  uint16_t y0 = luaL_checkinteger( L, 2 );
  uint16_t x1 = luaL_checkinteger( L, 3 );
  uint16_t y1 = luaL_checkinteger( L, 4 );
  uint16_t x2 = luaL_checkinteger( L, 5 );
  uint16_t y2 = luaL_checkinteger( L, 6 );
  uint16_t color = getColor( L, 7 );
  if (lua_gettop(L) > 7) TFT_fillTriangle(x0,y0,x1,y1,x2,y2,fillcolor);
  if (fillcolor != color) TFT_drawTriangle(x0,y0,x1,y1,x2,y2,color);
  return 0;
}

//lcd.write(x,y,string|intnum|{floatnum,dec},...)
//==================================
static int tft_write( lua_State* L )
{
  if (checkParam(3, L)) return 0;

  const char* buf;
  char tmps[16];
  size_t len;
  uint8_t numdec = 0;
  uint8_t argn = 0;
  float fnum;

  int x = luaL_checkinteger( L, 1 );
  int y = luaL_checkinteger( L, 2 );
  if ((x != LASTX) || (y != LASTY)) TFT_OFFSET = 0;
  if (x == LASTX) x = TFT_X;
  if (y == LASTY) y = TFT_Y;

  for( argn = 3; argn <= lua_gettop( L ); argn++ )
  {
    if ( lua_type( L, argn ) == LUA_TNUMBER )
    { // write integer number
      len = lua_tointeger( L, argn );
      sprintf(tmps,"%d",len);
      TFT_print(&tmps[0], x, y);
      x = TFT_X;
      y = TFT_Y;
    }
    else if ( lua_type( L, argn ) == LUA_TTABLE ) {
      if (lua_rawlen( L, argn ) == 2) {
        lua_rawgeti( L, argn, 1 );
        fnum = luaL_checknumber( L, -1 );
        lua_pop( L, 1 );
        lua_rawgeti( L, argn, 2 );
        numdec = ( int )luaL_checkinteger( L, -1 );
        lua_pop( L, 1 );
        sprintf(tmps,"%.*f",numdec, fnum);
        TFT_print(&tmps[0], x, y);
        x = TFT_X;
        y = TFT_Y;
      }
    }
    else if ( lua_type( L, argn ) == LUA_TSTRING )
    { // write string
      luaL_checktype( L, argn, LUA_TSTRING );
      buf = lua_tolstring( L, argn, &len );
      TFT_print((char*)buf, x, y);
      x = TFT_X;
      y = TFT_Y;
    }
  }
  return 0;
}


// ============= Touch panel functions =========================================

//-----------------------------------
static int _tp_get_data(uint8_t type)
{
	uint8_t txbuf[4] = {0};
	uint8_t rxbuf[4] = {0};
    esp_err_t ret;
    spi_transaction_t t;

    memset(&t, 0, sizeof(t));       //Zero out the transaction
    t.length=4*8;                   //Len is in bytes, transaction length is in bits.
    t.tx_buffer=txbuf;              //tx Data
    t.tx_buffer=rxbuf;              //rx Data
    ret=spi_device_transmit(touch_spi, &t);  //Transmit!

	if (ret == ESP_OK) return (uint32_t)( ((rxbuf[1] & 0x7F) << 5) | (rxbuf[2] >> 3) );
	else return -1;
}

//-----------------------------------------------
static int tp_get_data(uint8_t type, int samples)
{
	int n, result, val = 0;
	uint32_t i = 0;
	uint32_t vbuf[18];
	uint32_t minval, maxval, dif;
	//VM_TIME_UST_COUNT tmstart = vm_time_ust_get_count();
	//VM_TIME_UST_COUNT tmstop;

    if (samples < 3) samples = 1;
    if (samples > 18) samples = 18;

    // one dummy read
    result = _tp_get_data(type);

    // read data
	while (i < 10) {
    	minval = 5000;
    	maxval = 0;
		// get values
		for (n=0;n<samples;n++) {
		    result = _tp_get_data(type);
			if (result < 0) break;

			vbuf[n] = result;
			if (result < minval) minval = result;
			if (result > maxval) maxval = result;
		}
		if (result < 0) break;
		dif = maxval - minval;
		if (dif < 40) break;
		i++;
    }
	if (result < 0) return -1;

	if (samples > 2) {
		// remove one min value
		for (n = 0; n < samples; n++) {
			if (vbuf[n] == minval) {
				vbuf[n] = 5000;
				break;
			}
		}
		// remove one max value
		for (n = 0; n < samples; n++) {
			if (vbuf[n] == maxval) {
				vbuf[n] = 5000;
				break;
			}
		}
		for (n = 0; n < samples; n++) {
			if (vbuf[n] < 5000) val += vbuf[n];
		}
		val /= (samples-2);
	}
	else val = vbuf[0];

	/*
	tmstop = vm_time_ust_get_count();
	uint32_t dur;
	if (tmstop > tmstart) dur = tmstop - tmstart;
	else dur = tmstop + (0xFFFFFFFF - tmstart);
	//printf("Read %02X: time=%d, val=%d, min=%d, max=%d, dif=%d\n", type, dur, val, minval, maxval, (maxval-minval));
	*/

    return val;
}

//------------------------------------
static int tft_get_touch(lua_State *L)
{
	if (TFT_type != 1) {
		// touch available only on ILI9341
		lua_pushinteger(L, -1);
		return 1;
	}

	tft_spi_close();

	int result = -1;
    int X=0, Y=0, Z=0;

	if (touch_spi_init() != ESP_OK) goto exit;

    result = tp_get_data(0xB0, 3);
	if (result < 0) goto exit;

	if (result > 50)  {
		Z = result;

		result = tp_get_data(0xD0, 10);
		if (result < 0) goto exit;
		X = result;

		result = tp_get_data(0x90, 10);
		if (result < 0) goto exit;
		Y = result;
	}

exit:
	// reinitialize tft spi
	tft_init_spi();

	if (result >= 0) {
		lua_pushinteger(L, Z);
		lua_pushinteger(L, X);
		lua_pushinteger(L, Y);
		return 3;
	}
	else {
		lua_pushinteger(L, result);
		return 1;
	}
}

//=====================================
static int tft_read_touch(lua_State *L)
{
	if (TFT_type != 1) {
		// touch available only on ILI9341
		lua_pushinteger(L, -1);
		return 1;
	}

	tft_spi_close();

	int result = -1;
    int X=0, Y=0, tmp;

	if (touch_spi_init() != ESP_OK) goto exit;

    result = tp_get_data(0xB0, 3);
	if (result < 0) goto exit;

	if (result > 50)  {
		result = tp_get_data(0xD0, 10);
		if (result < 0) goto exit;
		X = result;

		result = tp_get_data(0x90, 10);
		if (result < 0) goto exit;
		Y = result;
	}

exit:
	// reinitialize tft spi
	tft_init_spi();

	if (result < 0) {
		lua_pushinteger(L, result);
		return 1;
	}

	int xleft = (tp_calx >> 16) & 0x3FFF;
	int xright = tp_calx & 0x3FFF;
	int ytop = (tp_caly >> 16) & 0x3FFF;
	int ybottom = tp_caly & 0x3FFF;

	X = ((X - xleft) * 320) / (xright - xleft);
	Y = ((Y - ytop) * 240) / (ybottom - ytop);

	if (X < 0) X = 0;
	if (X > 319) X = 319;
	if (Y < 0) Y = 0;
	if (Y > 239) Y = 239;

	switch (orientation) {
		case PORTRAIT:
			tmp = X;
			X = 240 - Y - 1;
			Y = tmp;
			break;
		case PORTRAIT_FLIP:
			tmp = X;
			X = Y;
			Y = 320 - tmp - 1;
			break;
		case LANDSCAPE_FLIP:
			X = 320 - X - 1;
			Y = 240 - Y - 1;
			break;
	}

	lua_pushinteger(L, X);
	lua_pushinteger(L, Y);
	return 2;
}

/*
//=======================================
static int lcd_set_touch_cs(lua_State *L)
{
	if (tp_cs_handle != VM_DCL_HANDLE_INVALID) {
    	vm_dcl_close(tp_cs_handle);
    	tp_cs_handle = VM_DCL_HANDLE_INVALID;
	}

    int cs = luaL_checkinteger(L, 1);
    gpio_get_handle(cs, &tp_cs_handle);
    if (tp_cs_handle != VM_DCL_HANDLE_INVALID) {
        vm_dcl_control(tp_cs_handle, VM_DCL_GPIO_COMMAND_SET_MODE_0, NULL);
        vm_dcl_control(tp_cs_handle, VM_DCL_GPIO_COMMAND_SET_DIRECTION_OUT, NULL);
        vm_dcl_control(tp_cs_handle, VM_DCL_GPIO_COMMAND_WRITE_HIGH, NULL);
        lua_pushinteger(L, 0);
    }
    else {
    	tp_cs_handle = VM_DCL_HANDLE_INVALID;
		vm_log_error("error initializing TP CS on pin #%d", cs);
	    lua_pushinteger(L, -1);
    }
    return 1;
}
*/

//==================================
static int tft_set_cal(lua_State *L)
{
    tp_calx = luaL_checkinteger(L, 1);
    tp_caly = luaL_checkinteger(L, 2);
    return 0;
}


// =============================================================================

#include "modules.h"

static const LUA_REG_TYPE tft_map[] = {
    { LSTRKEY( "init" ),			LFUNCVAL( ltft_init      ) },
	{ LSTRKEY( "clear" ),			LFUNCVAL( tft_clear )},
	{ LSTRKEY( "on" ),				LFUNCVAL( tft_on )},
	{ LSTRKEY( "off" ),				LFUNCVAL( tft_off )},
	{ LSTRKEY( "setfont" ),			LFUNCVAL( tft_setfont )},
	{ LSTRKEY( "compilefont" ),		LFUNCVAL( compile_font_file )},
	{ LSTRKEY( "getscreensize" ),	LFUNCVAL( tft_getscreensize )},
	{ LSTRKEY( "getfontsize" ),		LFUNCVAL( tft_getfontsize )},
	{ LSTRKEY( "getfontheight" ),	LFUNCVAL( tft_getfontheight )},
	{ LSTRKEY( "gettype" ),			LFUNCVAL( tft_gettype )},
	{ LSTRKEY( "setrot" ),			LFUNCVAL( tft_setrot )},
	{ LSTRKEY( "setorient" ),		LFUNCVAL( tft_setorient )},
	{ LSTRKEY( "setcolor" ),		LFUNCVAL( tft_setcolor )},
	{ LSTRKEY( "settransp" ),		LFUNCVAL( tft_settransp )},
	{ LSTRKEY( "setfixed" ),		LFUNCVAL( tft_setfixed )},
	{ LSTRKEY( "setwrap" ),			LFUNCVAL( tft_setwrap )},
	{ LSTRKEY( "setclipwin" ),		LFUNCVAL( tft_setclipwin )},
	{ LSTRKEY( "resetclipwin" ),	LFUNCVAL( tft_resetclipwin )},
	{ LSTRKEY( "invert" ),			LFUNCVAL( tft_invert )},
	{ LSTRKEY( "putpixel" ),		LFUNCVAL( tft_putpixel )},
	{ LSTRKEY( "line" ),			LFUNCVAL( tft_drawline )},
	{ LSTRKEY( "rect" ),			LFUNCVAL( tft_rect )},
	{ LSTRKEY( "circle" ),			LFUNCVAL( tft_circle )},
	{ LSTRKEY( "triangle" ),		LFUNCVAL( tft_triangle )},
	{ LSTRKEY( "write" ),			LFUNCVAL( tft_write )},
	{ LSTRKEY( "image" ),			LFUNCVAL( tft_image )},
	{ LSTRKEY( "jpgimage" ),		LFUNCVAL( ltft_jpg_image )},
	{ LSTRKEY( "bmpimage" ),		LFUNCVAL( tft_bmpimage )},
	{ LSTRKEY( "hsb2rgb" ),			LFUNCVAL( tft_HSBtoRGB )},
	{ LSTRKEY( "setbrightness" ),	LFUNCVAL( tft_set_brightness )},
	//{ LSTRKEY( "ontouch" ),			LFUNCVAL( tft_on_touch )},
	{ LSTRKEY( "gettouch" ),		LFUNCVAL( tft_read_touch )},
	{ LSTRKEY( "getrawtouch" ),		LFUNCVAL( tft_get_touch )},
	//{ LSTRKEY( "set_touch_cs" ),	LFUNCVAL( tft_set_touch_cs )},
	{ LSTRKEY( "setcal" ),			LFUNCVAL( tft_set_cal )},
#if LUA_USE_ROTABLE
	// Constant definitions
	  { LSTRKEY( "PORTRAIT" ),       LNUMVAL( PORTRAIT ) },
	  { LSTRKEY( "PORTRAIT_FLIP" ),  LNUMVAL( PORTRAIT_FLIP ) },
	  { LSTRKEY( "LANDSCAPE" ),      LNUMVAL( LANDSCAPE ) },
	  { LSTRKEY( "LANDSCAPE_FLIP" ), LNUMVAL( LANDSCAPE_FLIP ) },
	  { LSTRKEY( "CENTER" ),         LNUMVAL( CENTER ) },
	  { LSTRKEY( "RIGHT" ),          LNUMVAL( RIGHT ) },
	  { LSTRKEY( "LASTX" ),          LNUMVAL( LASTX ) },
	  { LSTRKEY( "LASTY" ),          LNUMVAL( LASTY ) },
	  { LSTRKEY( "BLACK" ),          LNUMVAL( TFT_BLACK ) },
	  { LSTRKEY( "NAVY" ),           LNUMVAL( TFT_NAVY ) },
	  { LSTRKEY( "DARKGREEN" ),      LNUMVAL( TFT_DARKGREEN ) },
	  { LSTRKEY( "DARKCYAN" ),       LNUMVAL( TFT_DARKCYAN ) },
	  { LSTRKEY( "MAROON" ),         LNUMVAL( TFT_MAROON ) },
	  { LSTRKEY( "PURPLE" ),         LNUMVAL( TFT_PURPLE ) },
	  { LSTRKEY( "OLIVE" ),          LNUMVAL( TFT_OLIVE ) },
	  { LSTRKEY( "LIGHTGREY" ),      LNUMVAL( TFT_LIGHTGREY ) },
	  { LSTRKEY( "DARKGREY" ),       LNUMVAL( TFT_DARKGREY ) },
	  { LSTRKEY( "BLUE" ),           LNUMVAL( TFT_BLUE ) },
	  { LSTRKEY( "GREEN" ),          LNUMVAL( TFT_GREEN ) },
	  { LSTRKEY( "CYAN" ),           LNUMVAL( TFT_CYAN ) },
	  { LSTRKEY( "RED" ),            LNUMVAL( TFT_RED ) },
	  { LSTRKEY( "MAGENTA" ),        LNUMVAL( TFT_MAGENTA ) },
	  { LSTRKEY( "YELLOW" ),         LNUMVAL( TFT_YELLOW ) },
	  { LSTRKEY( "WHITE" ),          LNUMVAL( TFT_WHITE ) },
	  { LSTRKEY( "ORANGE" ),         LNUMVAL( TFT_ORANGE ) },
	  { LSTRKEY( "GREENYELLOW" ),    LNUMVAL( TFT_GREENYELLOW ) },
	  { LSTRKEY( "PINK" ),           LNUMVAL( TFT_PINK ) },
	  { LSTRKEY( "FONT_DEFAULT" ),   LNUMVAL( DEFAULT_FONT ) },
	  { LSTRKEY( "FONT_7SEG" ),      LNUMVAL( FONT_7SEG ) },
	  { LSTRKEY( "ST7735" ),         LNUMVAL( 0 ) },
	  { LSTRKEY( "ST7735B" ),        LNUMVAL( 1 ) },
	  { LSTRKEY( "ST7735G" ),        LNUMVAL( 2 ) },
	  { LSTRKEY( "ILI9341" ),        LNUMVAL( 3 ) },
#endif
    { LNILKEY, LNILVAL }
};

int luaopen_tft(lua_State* L) {
#if !LUA_USE_ROTABLE
    luaL_newlib(L, screen_map);

    lua_pushinteger( L, 0 );
    lua_setfield( L, -2, "OrientationV0" );

    lua_pushinteger( L, 1 );
    lua_setfield( L, -2, "OrientationV1" );

    lua_pushinteger( L, 2 );
    lua_setfield( L, -2, "OrientationH0" );

    lua_pushinteger( L, 3 );
    lua_setfield( L, -2, "OrientationH1" );

    return 1;
#else
	return 0;
#endif
}

MODULE_REGISTER_MAPPED(TFT, tft, tft_map, luaopen_tft);
