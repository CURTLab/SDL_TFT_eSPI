/****************************************************************************
 *
 * Copyright (C) 2018 - 2022 Fabian Hauser
 *
 * Author: Fabian Hauser <fabian.hauser@fh-linz.at>
 * University of Applied Sciences Upper Austria - Linz - Austra
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 ****************************************************************************/
#include "TFT_eSPI.h"

#include <SDL.h>
#include <assert.h>
#include <stdexcept>

// Clipping macro for pushImage
#define PI_CLIP                                        \
	if (_vpOoB) return;                                  \
	x+= _xDatum;                                         \
	y+= _yDatum;                                         \
	\
	if ((x >= _vpW) || (y >= _vpH)) return;              \
	\
	int32_t dx = 0;                                      \
	int32_t dy = 0;                                      \
	int32_t dw = w;                                      \
	int32_t dh = h;                                      \
	\
	if (x < _vpX) { dx = _vpX - x; dw -= dx; x = _vpX; } \
	if (y < _vpY) { dy = _vpY - y; dh -= dy; y = _vpY; } \
	\
	if ((x + dw) > _vpW ) dw = _vpW - x;                 \
	if ((y + dh) > _vpH ) dh = _vpH - y;                 \
	\
	if (dw < 1 || dh < 1) return;



#include "Extensions/Button.cpp"
#include "Extensions/Sprite.cpp"

TFT_eSPI::TFT_eSPI(int16_t w, int16_t h)
{
	_init_width  = _width  = w; // Set by specific xxxxx_Defines.h file or by users sketch
	_init_height = _height = h; // Set by specific xxxxx_Defines.h file or by users sketch

	// Reset the viewport to the whole screen
	resetViewport();

	rotation  = 0;
	cursor_y  = cursor_x  = last_cursor_x = bg_cursor_x = 0;
	textfont  = 1;
	textsize  = 1;
	textcolor   = bitmap_fg = 0xFFFF; // White
	textbgcolor = bitmap_bg = 0x0000; // Black
	padX        = 0;                  // No padding

	_fillbg    = false;   // Smooth font only at the moment, force text background fill

	isDigits   = false;   // No bounding box adjustment
	textwrapX  = true;    // Wrap text at end of line when using print stream
	textwrapY  = false;   // Wrap text at bottom of screen when using print stream
	textdatum = TL_DATUM; // Top Left text alignment is default
	fontsloaded = 0;

	_swapBytes = false;   // Do not swap colour bytes by default

	locked = true;           // Transaction mutex lock flag to ensure begin/endTranaction pairing
	inTransaction = false;   // Flag to prevent multiple sequential functions to keep bus access open
	lockTransaction = false; // start/endWrite lock flag to allow sketch to keep SPI bus access open

	_booted   = true;     // Default attributes
	_cp437    = true;     // Legacy GLCD font bug fix
	_utf8     = true;     // UTF8 decoding enabled

#if defined (FONT_FS_AVAILABLE) && defined (SMOOTH_FONT)
	fs_font  = true;     // Smooth font filing system or array (fs_font = false) flag
#endif

#if defined (ESP32) && defined (CONFIG_SPIRAM_SUPPORT)
	if (psramFound()) _psram_enable = true; // Enable the use of PSRAM (if available)
	else
#endif
		_psram_enable = false;

	addr_row = 0xFFFF;  // drawPixel command length optimiser
	addr_col = 0xFFFF;  // drawPixel command length optimiser

	_xPivot = 0;
	_yPivot = 0;

	// Legacy support for bit GPIO masks
	cspinmask = 0;
	dcpinmask = 0;
	wrpinmask = 0;
	sclkpinmask = 0;

	// Flags for which fonts are loaded
#ifdef LOAD_GLCD
	fontsloaded  = 0x0002; // Bit 1 set
#endif

#ifdef LOAD_FONT2
	fontsloaded |= 0x0004; // Bit 2 set
#endif

#ifdef LOAD_FONT4
	fontsloaded |= 0x0010; // Bit 4 set
#endif

#ifdef LOAD_FONT6
	fontsloaded |= 0x0040; // Bit 6 set
#endif

#ifdef LOAD_FONT7
	fontsloaded |= 0x0080; // Bit 7 set
#endif

#ifdef LOAD_FONT8
	fontsloaded |= 0x0100; // Bit 8 set
#endif

#ifdef LOAD_FONT8N
	fontsloaded |= 0x0200; // Bit 9 set
#endif

#ifdef SMOOTH_FONT
	fontsloaded |= 0x8000; // Bit 15 set
#endif
}

static SDL_Window *SDL_WINDOW;
static SDL_Renderer *SDL_RENDERER;

void setSDLColor(uint32_t color)
{
	uint8_t r = (color >> 8) & 0xF8; r |= (r >> 5);
	uint8_t g = (color >> 3) & 0xFC; g |= (g >> 6);
	uint8_t b = (color << 3) & 0xF8; b |= (b >> 5);

	SDL_SetRenderDrawColor(SDL_RENDERER, r, g, b, 255);
}

TFT_eSPI::~TFT_eSPI()
{
	SDL_DestroyWindow(SDL_WINDOW);
	SDL_DestroyRenderer(SDL_RENDERER);
}

void TFT_eSPI::init(uint8_t tc)
{
	if (SDL_Init(SDL_INIT_EVERYTHING) != 0){
		throw std::runtime_error("SDL_Init failed: " + std::string(SDL_GetError()));
	}

	SDL_WINDOW = SDL_CreateWindow("Arduino", SDL_WINDOWPOS_CENTERED,
											SDL_WINDOWPOS_CENTERED, _init_width, _init_height, 0);

	SDL_RENDERER = SDL_CreateRenderer(SDL_WINDOW, -1, 0);

	setRotation(rotation);
}

void TFT_eSPI::begin(uint8_t tc)
{
	init(tc);
}

void TFT_eSPI::loop()
{
	SDL_Event event;
	SDL_PollEvent(&event);
	/*while(SDL_PollEvent(&event)) {
		if (event.type == SDL_QUIT) {
			throw std::runtime_error("exit");
		} else if (event.type == SDL_KEYUP) {
			if (event.key.keysym.sym == SDLK_ESCAPE)
				throw std::runtime_error("exit");
		}
	}*/
}

void TFT_eSPI::drawPixel(int32_t x, int32_t y, uint32_t color)
{
	if (_vpOoB) return;

	x+= _xDatum;
	y+= _yDatum;

	// Range checking
	if ((x < _vpX) || (y < _vpY) ||(x >= _vpW) || (y >= _vpH)) return;

#ifdef CGRAM_OFFSET
	x+=colstart;
	y+=rowstart;
#endif

	setSDLColor(color);
	SDL_RenderDrawPoint(SDL_RENDERER, x, y);

	SDL_RenderPresent(SDL_RENDERER);
}

void TFT_eSPI::drawChar(int32_t x, int32_t y, uint16_t c, uint32_t color, uint32_t bg, uint8_t size)
{
	assert(false && "drawChar not implemented yet");
}

void TFT_eSPI::drawLine(int32_t xs, int32_t ys, int32_t xe, int32_t ye, uint32_t color)
{
	if (_vpOoB) return;

	setSDLColor(color);
	SDL_RenderDrawLine(SDL_RENDERER, xs, ys, xe, ye);
	SDL_RenderPresent(SDL_RENDERER);
}

void TFT_eSPI::drawFastVLine(int32_t x, int32_t y, int32_t h, uint32_t color)
{
	setSDLColor(color);
	SDL_RenderDrawLine(SDL_RENDERER, x, y, x, y + h);
}

void TFT_eSPI::drawFastHLine(int32_t x, int32_t y, int32_t w, uint32_t color)
{
	setSDLColor(color);
	SDL_RenderDrawLine(SDL_RENDERER, x, y, x + w, y);
}

void TFT_eSPI::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color)
{
	if (_vpOoB)
		return;

	x+= _xDatum;
	y+= _yDatum;

	// Clipping
	if ((x >= _vpW) || (y >= _vpH))
		return;

	if (x < _vpX) { w += x - _vpX; x = _vpX; }
	if (y < _vpY) { h += y - _vpY; y = _vpY; }

	if ((x + w) > _vpW) w = _vpW - x;
	if ((y + h) > _vpH) h = _vpH - y;

	if ((w < 1) || (h < 1))
		return;

	//Serial.print(" _xDatum=");Serial.print( _xDatum);Serial.print(", _yDatum=");Serial.print( _yDatum);
	//Serial.print(", _xWidth=");Serial.print(_xWidth);Serial.print(", _yHeight=");Serial.println(_yHeight);

	//Serial.print(" _vpX=");Serial.print( _vpX);Serial.print(", _vpY=");Serial.print( _vpY);
	//Serial.print(", _vpW=");Serial.print(_vpW);Serial.print(", _vpH=");Serial.println(_vpH);

	//Serial.print(" x=");Serial.print( y);Serial.print(", y=");Serial.print( y);
	//Serial.print(", w=");Serial.print(w);Serial.print(", h=");Serial.println(h);

	setSDLColor(color);
	SDL_Rect rect = {x, y, w, h};
	SDL_RenderFillRect(SDL_RENDERER, &rect);

	SDL_RenderPresent(SDL_RENDERER);

	/*begin_tft_write();

	setWindow(x, y, x + w - 1, y + h - 1);

	pushBlock(color, w * h);

	end_tft_write();*/
}

int16_t TFT_eSPI::drawChar(uint16_t uniCode, int32_t x, int32_t y, uint8_t font)
{
	if (_vpOoB || !uniCode)
		return 0;

	if (font==1) {
#ifdef LOAD_GLCD
#ifndef LOAD_GFXFF
		drawChar(x, y, uniCode, textcolor, textbgcolor, textsize);
		return 6 * textsize;
#endif
#else
#ifndef LOAD_GFXFF
		return 0;
#endif
#endif

#ifdef LOAD_GFXFF
		drawChar(x, y, uniCode, textcolor, textbgcolor, textsize);
		if(!gfxFont) { // 'Classic' built-in font
#ifdef LOAD_GLCD
			return 6 * textsize;
#else
			return 0;
#endif
		}
		else {
			if((uniCode >= pgm_read_word(&gfxFont->first)) && (uniCode <= pgm_read_word(&gfxFont->last) )) {
				uint16_t   c2    = uniCode - pgm_read_word(&gfxFont->first);
				GFXglyph *glyph = &(((GFXglyph *)pgm_read_dword(&gfxFont->glyph))[c2]);
				return pgm_read_byte(&glyph->xAdvance) * textsize;
			}
			else {
				return 0;
			}
		}
#endif
	}

	if ((font>1) && (font<9) && ((uniCode < 32) || (uniCode > 127))) return 0;

	int32_t width  = 0;
	int32_t height = 0;
	uint32_t flash_address = 0;
	uniCode -= 32;

#ifdef LOAD_FONT2
	if (font == 2) {
		flash_address = pgm_read_dword(&chrtbl_f16[uniCode]);
		width = pgm_read_byte(widtbl_f16 + uniCode);
		height = chr_hgt_f16;
	}
#ifdef LOAD_RLE
	else
#endif
#endif

#ifdef LOAD_RLE
	{
		if ((font>2) && (font<9)) {
			flash_address = pgm_read_dword( (const void*)(pgm_read_dword( &(fontdata[font].chartbl ) ) + uniCode*sizeof(void *)) );
			width = pgm_read_byte( (uint8_t *)pgm_read_dword( &(fontdata[font].widthtbl ) ) + uniCode );
			height= pgm_read_byte( &fontdata[font].height );
		}
	}
#endif

	int32_t xd = x + _xDatum;
	int32_t yd = y + _yDatum;

	if ((xd + width * textsize < _vpX || xd >= _vpW) && (yd + height * textsize < _vpY || yd >= _vpH)) return width * textsize ;

	int32_t w = width;
	int32_t pX      = 0;
	int32_t pY      = y;
	uint8_t line = 0;
	bool clip = xd < _vpX || xd + width  * textsize >= _vpW || yd < _vpY || yd + height * textsize >= _vpH;

#ifdef LOAD_FONT2 // chop out code if we do not need it
	if (font == 2) {
		w = w + 6; // Should be + 7 but we need to compensate for width increment
		w = w / 8;

		if (textcolor == textbgcolor || textsize != 1 || clip) {
			//begin_tft_write();          // Sprite class can use this function, avoiding begin_tft_write()
			inTransaction = true;

			for (int32_t i = 0; i < height; i++) {
				if (textcolor != textbgcolor) fillRect(x, pY, width * textsize, textsize, textbgcolor);

				for (int32_t k = 0; k < w; k++) {
					line = pgm_read_byte((uint8_t *)flash_address + w * i + k);
					if (line) {
						if (textsize == 1) {
							pX = x + k * 8;
							if (line & 0x80) drawPixel(pX, pY, textcolor);
							if (line & 0x40) drawPixel(pX + 1, pY, textcolor);
							if (line & 0x20) drawPixel(pX + 2, pY, textcolor);
							if (line & 0x10) drawPixel(pX + 3, pY, textcolor);
							if (line & 0x08) drawPixel(pX + 4, pY, textcolor);
							if (line & 0x04) drawPixel(pX + 5, pY, textcolor);
							if (line & 0x02) drawPixel(pX + 6, pY, textcolor);
							if (line & 0x01) drawPixel(pX + 7, pY, textcolor);
						}
						else {
							pX = x + k * 8 * textsize;
							if (line & 0x80) fillRect(pX, pY, textsize, textsize, textcolor);
							if (line & 0x40) fillRect(pX + textsize, pY, textsize, textsize, textcolor);
							if (line & 0x20) fillRect(pX + 2 * textsize, pY, textsize, textsize, textcolor);
							if (line & 0x10) fillRect(pX + 3 * textsize, pY, textsize, textsize, textcolor);
							if (line & 0x08) fillRect(pX + 4 * textsize, pY, textsize, textsize, textcolor);
							if (line & 0x04) fillRect(pX + 5 * textsize, pY, textsize, textsize, textcolor);
							if (line & 0x02) fillRect(pX + 6 * textsize, pY, textsize, textsize, textcolor);
							if (line & 0x01) fillRect(pX + 7 * textsize, pY, textsize, textsize, textcolor);
						}
					}
				}
				pY += textsize;
			}

			inTransaction = lockTransaction;
			end_tft_write();
		}
		else { // Faster drawing of characters and background using block write

			begin_tft_write();

			setWindow(xd, yd, xd + width - 1, yd + height - 1);

			uint8_t mask;
			for (int32_t i = 0; i < height; i++) {
				pX = width;
				for (int32_t k = 0; k < w; k++) {
					line = pgm_read_byte((uint8_t *) (flash_address + w * i + k) );
					mask = 0x80;
					while (mask && pX) {
						if (line & mask) {tft_Write_16(textcolor);}
						else {tft_Write_16(textbgcolor);}
						pX--;
						mask = mask >> 1;
					}
				}
				if (pX) {tft_Write_16(textbgcolor);}
			}

			end_tft_write();
		}
	}

#ifdef LOAD_RLE
	else
#endif
#endif  //FONT2

#ifdef LOAD_RLE  //674 bytes of code
		// Font is not 2 and hence is RLE encoded
	{
		begin_tft_write();
		inTransaction = true;

		w *= height; // Now w is total number of pixels in the character
		if (textcolor == textbgcolor && !clip) {

			int32_t px = 0, py = pY; // To hold character block start and end column and row values
			int32_t pc = 0; // Pixel count
			uint8_t np = textsize * textsize; // Number of pixels in a drawn pixel

			uint8_t tnp = 0; // Temporary copy of np for while loop
			uint8_t ts = textsize - 1; // Temporary copy of textsize
			// 16 bit pixel count so maximum font size is equivalent to 180x180 pixels in area
			// w is total number of pixels to plot to fill character block
			while (pc < w) {
				line = pgm_read_byte((uint8_t *)flash_address);
				flash_address++;
				if (line & 0x80) {
					line &= 0x7F;
					line++;
					if (ts) {
						px = xd + textsize * (pc % width); // Keep these px and py calculations outside the loop as they are slow
						py = yd + textsize * (pc / width);
					}
					else {
						px = xd + pc % width; // Keep these px and py calculations outside the loop as they are slow
						py = yd + pc / width;
					}
					while (line--) { // In this case the while(line--) is faster
						pc++; // This is faster than putting pc+=line before while()?
						setWindow(px, py, px + ts, py + ts);

						if (ts) {
							tnp = np;
							while (tnp--) {tft_Write_16(textcolor);}
						}
						else {tft_Write_16(textcolor);}
						px += textsize;

						if (px >= (xd + width * textsize)) {
							px = xd;
							py += textsize;
						}
					}
				}
				else {
					line++;
					pc += line;
				}
			}
		}
		else {
			// Text colour != background and textsize = 1 and character is within viewport area
			// so use faster drawing of characters and background using block write
			if (textcolor != textbgcolor && textsize == 1 && !clip)
			{
				setWindow(xd, yd, xd + width - 1, yd + height - 1);

				// Maximum font size is equivalent to 180x180 pixels in area
				while (w > 0) {
					line = pgm_read_byte((uint8_t *)flash_address++); // 8 bytes smaller when incrementing here
					if (line & 0x80) {
						line &= 0x7F;
						line++; w -= line;
						pushBlock(textcolor,line);
					}
					else {
						line++; w -= line;
						pushBlock(textbgcolor,line);
					}
				}
			}
			else
			{
				int32_t px = 0, py = 0;  // To hold character pixel coords
				int32_t tx = 0, ty = 0;  // To hold character TFT pixel coords
				int32_t pc = 0;          // Pixel count
				int32_t pl = 0;          // Pixel line length
				uint16_t pcol = 0;       // Pixel color
				bool     pf = true;      // Flag for plotting
				while (pc < w) {
					line = pgm_read_byte((uint8_t *)flash_address);
					flash_address++;
					if (line & 0x80) { pcol = textcolor; line &= 0x7F; pf = true;}
					else { pcol = textbgcolor; if (textcolor == textbgcolor) pf = false;}
					line++;
					px = pc % width;
					tx = x + textsize * px;
					py = pc / width;
					ty = y + textsize * py;

					pl = 0;
					pc += line;
					while (line--) {
						pl++;
						if ((px+pl) >= width) {
							if (pf) fillRect(tx, ty, pl * textsize, textsize, pcol);
							pl = 0;
							px = 0;
							tx = x;
							py ++;
							ty += textsize;
						}
					}
					if (pl && pf) fillRect(tx, ty, pl * textsize, textsize, pcol);
				}
			}
		}
		inTransaction = lockTransaction;
		end_tft_write();
	}
	// End of RLE font rendering
#endif

#if !defined (LOAD_FONT2) && !defined (LOAD_RLE)
	// Stop warnings
	flash_address = flash_address;
	w = w;
	pX = pX;
	pY = pY;
	line = line;
	clip = clip;
#endif

	return width * textsize;    // x +
}

int16_t TFT_eSPI::drawChar(uint16_t uniCode, int32_t x, int32_t y)
{
	return drawChar(uniCode, x, y, textfont);
}

int16_t TFT_eSPI::height(void)
{
	if (_vpDatum) return _yHeight;
	return _height;
}

int16_t TFT_eSPI::width(void)
{
	if (_vpDatum) return _xWidth;
	return _width;
}

uint16_t TFT_eSPI::readPixel(int32_t x, int32_t y)
{
	assert(false && "readPixel not implemented yet");
	return 0;
}

void TFT_eSPI::setWindow(int32_t xs, int32_t ys, int32_t xe, int32_t ye)
{
	assert(false && "setWindow not implemented yet");
}

void TFT_eSPI::pushColor(uint16_t color)
{

}

void TFT_eSPI::begin_nin_write()
{

}

void TFT_eSPI::end_nin_write()
{

}

void TFT_eSPI::setRotation(uint8_t r)
{
	rotation = r;
	// Reset the viewport to the whole screen
	resetViewport();
}

uint8_t TFT_eSPI::getRotation()
{
	return rotation;
}

void TFT_eSPI::invertDisplay(bool i)
{
	assert(false && "invertDisplay not implemented yet");
}

void TFT_eSPI::setAddrWindow(int32_t xs, int32_t ys, int32_t w, int32_t h)
{
	assert(false && "setAddrWindow not implemented yet");
}

void TFT_eSPI::setViewport(int32_t x, int32_t y, int32_t w, int32_t h, bool vpDatum)
{
	// Viewport metrics (not clipped)
	_xDatum  = x; // Datum x position in screen coordinates
	_yDatum  = y; // Datum y position in screen coordinates
	_xWidth  = w; // Viewport width
	_yHeight = h; // Viewport height

	// Full size default viewport
	_vpDatum = false; // Datum is at top left corner of screen (true = top left of viewport)
	_vpOoB   = false; // Out of Bounds flag (true is all of viewport is off screen)
	_vpX = 0;         // Viewport top left corner x coordinate
	_vpY = 0;         // Viewport top left corner y coordinate
	_vpW = width();   // Equivalent of TFT width  (Nb: viewport right edge coord + 1)
	_vpH = height();  // Equivalent of TFT height (Nb: viewport bottom edge coord + 1)

	// Clip viewport to screen area
	if (x<0) { w += x; x = 0; }
	if (y<0) { h += y; y = 0; }
	if ((x + w) > width() ) { w = width()  - x; }
	if ((y + h) > height() ) { h = height() - y; }

	//Serial.print(" x=");Serial.print( x);Serial.print(", y=");Serial.print( y);
	//Serial.print(", w=");Serial.print(w);Serial.print(", h=");Serial.println(h);

	// Check if viewport is entirely out of bounds
	if (w < 1 || h < 1)
	{
		// Set default values and Out of Bounds flag in case of error
		_xDatum = 0;
		_yDatum = 0;
		_xWidth  = width();
		_yHeight = height();
		_vpOoB = true;      // Set Out of Bounds flag to inhibit all drawing
		return;
	}

	if (!vpDatum)
	{
		_xDatum = 0; // Reset to top left of screen if not using a viewport datum
		_yDatum = 0;
		_xWidth  = width();
		_yHeight = height();
	}

	// Store the clipped screen viewport metrics and datum position
	_vpX = x;
	_vpY = y;
	_vpW = x + w;
	_vpH = y + h;
	_vpDatum = vpDatum;
}

bool TFT_eSPI::checkViewport(int32_t x, int32_t y, int32_t w, int32_t h)
{
	if (_vpOoB) return false;
	x+= _xDatum;
	y+= _yDatum;

	if ((x >= _vpW) || (y >= _vpH)) return false;

	int32_t dx = 0;
	int32_t dy = 0;
	int32_t dw = w;
	int32_t dh = h;

	if (x < _vpX) { dx = _vpX - x; dw -= dx; x = _vpX; }
	if (y < _vpY) { dy = _vpY - y; dh -= dy; y = _vpY; }

	if ((x + dw) > _vpW ) dw = _vpW - x;
	if ((y + dh) > _vpH ) dh = _vpH - y;

	if (dw < 1 || dh < 1) return false;

	return true;
}

int32_t TFT_eSPI::getViewportX()
{
	return _xDatum;
}

int32_t TFT_eSPI::getViewportY()
{
	return _yDatum;
}

int32_t TFT_eSPI::getViewportWidth()
{
	return _xWidth;
}

int32_t TFT_eSPI::getViewportHeight()
{
	return _yHeight;
}

bool TFT_eSPI::getViewportDatum()
{
	return _vpDatum;
}

void TFT_eSPI::frameViewport(uint16_t color, int32_t w)
{
	// Save datum position
	bool _dT = _vpDatum;

	// If w is positive the frame is drawn inside the viewport
	// a large positive width will clear the screen inside the viewport
	if (w>0)
	{
		// Set vpDatum true to simplify coordinate derivation
		_vpDatum = true;
		fillRect(0, 0, _vpW - _vpX, w, color);                // Top
		fillRect(0, w, w, _vpH - _vpY - w - w, color);        // Left
		fillRect(_xWidth - w, w, w, _yHeight - w - w, color); // Right
		fillRect(0, _yHeight - w, _xWidth, w, color);         // Bottom
	}
	else
		// If w is negative the frame is drawn outside the viewport
		// a large negative width will clear the screen outside the viewport
	{
		w = -w;

		// Save old values
		int32_t _xT = _vpX; _vpX = 0;
		int32_t _yT = _vpY; _vpY = 0;
		int32_t _wT = _vpW;
		int32_t _hT = _vpH;

		// Set vpDatum false so frame can be drawn outside window
		_vpDatum = false; // When false the full width and height is accessed
		_vpH = height();
		_vpW = width();

		// Draw frame
		fillRect(_xT - w - _xDatum, _yT - w - _yDatum, _wT - _xT + w + w, w, color); // Top
		fillRect(_xT - w - _xDatum, _yT - _yDatum, w, _hT - _yT, color);             // Left
		fillRect(_wT - _xDatum, _yT - _yDatum, w, _hT - _yT, color);                 // Right
		fillRect(_xT - w - _xDatum, _hT - _yDatum, _wT - _xT + w + w, w, color);     // Bottom

		// Restore old values
		_vpX = _xT;
		_vpY = _yT;
		_vpW = _wT;
		_vpH = _hT;
	}

	// Restore vpDatum
	_vpDatum = _dT;
}

void TFT_eSPI::resetViewport()
{
	// Reset viewport to the whole screen (or sprite) area
	_vpDatum = false;
	_vpOoB   = false;
	_xDatum = 0;
	_yDatum = 0;
	_vpX = 0;
	_vpY = 0;
	_vpW = width();
	_vpH = height();
	_xWidth  = width();
	_yHeight = height();
}

bool TFT_eSPI::clipAddrWindow(int32_t *x, int32_t *y, int32_t *w, int32_t *h)
{
	if (_vpOoB) return false; // Area is outside of viewport

	*x+= _xDatum;
	*y+= _yDatum;

	if ((*x >= _vpW) || (*y >= _vpH)) return false;  // Area is outside of viewport

	// Crop drawing area bounds
	if (*x < _vpX) { *w -= _vpX - *x; *x = _vpX; }
	if (*y < _vpY) { *h -= _vpY - *y; *y = _vpY; }

	if ((*x + *w) > _vpW ) *w = _vpW - *x;
	if ((*y + *h) > _vpH ) *h = _vpH - *y;

	if (*w < 1 || *h < 1) return false; // No area is inside viewport

	return true;  // Area is wholly or partially inside viewport
}

bool TFT_eSPI::clipWindow(int32_t *xs, int32_t *ys, int32_t *xe, int32_t *ye)
{
	if (_vpOoB) return false; // Area is outside of viewport

	*xs+= _xDatum;
	*ys+= _yDatum;
	*xe+= _xDatum;
	*ye+= _yDatum;

	if ((*xs >= _vpW) || (*ys >= _vpH)) return false;  // Area is outside of viewport
	if ((*xe <  _vpX) || (*ye <  _vpY)) return false;  // Area is outside of viewport

	// Crop drawing area bounds
	if (*xs < _vpX) *xs = _vpX;
	if (*ys < _vpY) *ys = _vpY;

	if (*xe > _vpW) *xe = _vpW - 1;
	if (*ye > _vpH) *ye = _vpH - 1;

	return true;  // Area is wholly or partially inside viewport
}

void TFT_eSPI::pushColor(uint16_t color, uint32_t len)  // Deprecated, use pushBlock()
{
	assert(false && "pushColor not implemented yet");
}

void TFT_eSPI::pushColors(uint16_t  *data, uint32_t len, bool swap) // With byte swap option
{
	assert(false && "pushColors not implemented yet");
}

void TFT_eSPI::pushColors(uint8_t  *data, uint32_t len) // Deprecated, use pushPixels()
{
	assert(false && "pushColors not implemented yet");
}

// Write a solid block of a single colour
void TFT_eSPI::pushBlock(uint16_t color, uint32_t len)
{
	assert(false && "pushBlock not implemented yet");
}

// Write a set of pixels stored in memory, use setSwapBytes(true/false) function to correct endianess
void TFT_eSPI::pushPixels(const void * data_in, uint32_t len)
{
	assert(false && "pushPixels not implemented yet");
}

void TFT_eSPI::fillScreen(uint32_t color)
{
	fillRect(0, 0, _width, _height, color);
}

void TFT_eSPI::drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color)
{
	assert(false && "drawRect not implemented yet");
}

void TFT_eSPI::drawRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t radius, uint32_t color)
{
	assert(false && "drawRoundRect not implemented yet");
}

void TFT_eSPI::fillRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t radius, uint32_t color)
{
	assert(false && "fillRoundRect not implemented yet");
}

void TFT_eSPI::fillRectVGradient(int16_t x, int16_t y, int16_t w, int16_t h, uint32_t color1, uint32_t color2)
{
	assert(false && "fillRectVGradient not implemented yet");
}

void TFT_eSPI::fillRectHGradient(int16_t x, int16_t y, int16_t w, int16_t h, uint32_t color1, uint32_t color2)
{
	assert(false && "fillRectHGradient not implemented yet");
}

uint16_t TFT_eSPI::drawPixel(int32_t x, int32_t y, uint32_t color, uint8_t alpha, uint32_t bg_color)
{
	return 0;
}

void TFT_eSPI::drawSpot(float ax, float ay, float r, uint32_t fg_color, uint32_t bg_color)
{
	assert(false && "drawSpot not implemented yet");
}

void TFT_eSPI::fillSmoothCircle(int32_t x, int32_t y, int32_t r, uint32_t color, uint32_t bg_color)
{
	assert(false && "fillSmoothCircle not implemented yet");
}

void TFT_eSPI::fillSmoothRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t radius, uint32_t color, uint32_t bg_color)
{
	assert(false && "fillSmoothRoundRect not implemented yet");
}

void TFT_eSPI::drawWideLine(float ax, float ay, float bx, float by, float wd, uint32_t fg_color, uint32_t bg_color)
{
	assert(false && "drawWideLine not implemented yet");
}

void TFT_eSPI::drawWedgeLine(float ax, float ay, float bx, float by, float aw, float bw, uint32_t fg_color, uint32_t bg_color)
{
	assert(false && "drawWedgeLine not implemented yet");
}

void TFT_eSPI::drawCircle(int32_t x, int32_t y, int32_t r, uint32_t color)
{
	assert(false && "drawCircle not implemented yet");
}

void TFT_eSPI::drawCircleHelper(int32_t x, int32_t y, int32_t r, uint8_t cornername, uint32_t color)
{
	assert(false && "drawCircleHelper not implemented yet");
}

void TFT_eSPI::fillCircle(int32_t x0, int32_t y0, int32_t r, uint32_t color)
{
	int32_t  x  = 0;
	int32_t  dx = 1;
	int32_t  dy = r+r;
	int32_t  p  = -(r>>1);

	drawFastHLine(x0 - r, y0, dy+1, color);

	while(x<r){

		if(p>=0) {
			drawFastHLine(x0 - x, y0 + r, dx, color);
			drawFastHLine(x0 - x, y0 - r, dx, color);
			dy-=2;
			p-=dy;
			r--;
		}

		dx+=2;
		p+=dx;
		x++;

		drawFastHLine(x0 - r, y0 + x, dy+1, color);
		drawFastHLine(x0 - r, y0 - x, dy+1, color);

	}

	SDL_RenderPresent(SDL_RENDERER);
}

void TFT_eSPI::fillCircleHelper(int32_t x, int32_t y, int32_t r, uint8_t cornername, int32_t delta, uint32_t color)
{
	assert(false && "fillCircleHelper not implemented yet");
}

void TFT_eSPI::drawEllipse(int16_t x, int16_t y, int32_t rx, int32_t ry, uint16_t color)
{
	assert(false && "drawEllipse not implemented yet");
}

void TFT_eSPI::fillEllipse(int16_t x, int16_t y, int32_t rx, int32_t ry, uint16_t color)
{
	assert(false && "fillEllipse not implemented yet");
}

void TFT_eSPI::drawTriangle(int32_t x1,int32_t y1, int32_t x2,int32_t y2, int32_t x3,int32_t y3, uint32_t color)
{
	assert(false && "drawTriangle not implemented yet");
}

void TFT_eSPI::fillTriangle(int32_t x1,int32_t y1, int32_t x2,int32_t y2, int32_t x3,int32_t y3, uint32_t color)
{
	assert(false && "fillTriangle not implemented yet");
}

void TFT_eSPI::setSwapBytes(bool swap)
{

}

bool TFT_eSPI::getSwapBytes()
{
	return 0;
}

void TFT_eSPI::drawBitmap( int16_t x, int16_t y, const uint8_t *bitmap, int16_t w, int16_t h, uint16_t fgcolor)
{
	assert(false && "drawBitmap not implemented yet");
}

void TFT_eSPI::drawBitmap( int16_t x, int16_t y, const uint8_t *bitmap, int16_t w, int16_t h, uint16_t fgcolor, uint16_t bgcolor)
{
	assert(false && "drawBitmap not implemented yet");
}

void TFT_eSPI::drawXBitmap(int16_t x, int16_t y, const uint8_t *bitmap, int16_t w, int16_t h, uint16_t fgcolor)
{
	assert(false && "drawXBitmap not implemented yet");
}

void TFT_eSPI::drawXBitmap(int16_t x, int16_t y, const uint8_t *bitmap, int16_t w, int16_t h, uint16_t fgcolor, uint16_t bgcolor)
{
	assert(false && "drawXBitmap not implemented yet");
}

void TFT_eSPI::setBitmapColor(uint16_t fgcolor, uint16_t bgcolor)
{
	assert(false && "setBitmapColor not implemented yet");
}

void TFT_eSPI::setPivot(int16_t x, int16_t y)
{
	assert(false && "setPivot not implemented yet");
}

int16_t TFT_eSPI::getPivotX(void)
{
	assert(false && "getPivotX not implemented yet");
	return 0;
}

int16_t TFT_eSPI::getPivotY(void)
{
	assert(false && "getPivotY not implemented yet");
	return 0;
}

void TFT_eSPI::readRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t *data)
{
	assert(false && "readRect not implemented yet");
}

void TFT_eSPI::pushRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t *data)
{
	assert(false && "pushRect not implemented yet");
}

void TFT_eSPI::pushImage(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t *data)
{
	assert(false && "pushImage not implemented yet");
}

void TFT_eSPI::pushImage(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t *data, uint16_t transparent)
{
	assert(false && "pushImage not implemented yet");
}

void TFT_eSPI::pushImage(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t *data, uint16_t transparent)
{
	assert(false && "pushImage not implemented yet");
}

void TFT_eSPI::pushImage(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t *data)
{
	assert(false && "pushImage not implemented yet");
}

void TFT_eSPI::pushImage(int32_t x, int32_t y, int32_t w, int32_t h, uint8_t *data, bool bpp8, uint16_t *cmap)
{
	assert(false && "pushImage not implemented yet");
}

void TFT_eSPI::pushImage(int32_t x, int32_t y, int32_t w, int32_t h, uint8_t *data, uint8_t transparent, bool bpp8, uint16_t *cmap)
{
	assert(false && "pushImage not implemented yet");
}

void TFT_eSPI::pushImage(int32_t x, int32_t y, int32_t w, int32_t h, const uint8_t *data, bool bpp8, uint16_t *cmap)
{
	assert(false && "pushImage not implemented yet");
}

void TFT_eSPI::readRectRGB(int32_t x, int32_t y, int32_t w, int32_t h, uint8_t *data)
{
	assert(false && "readRectRGB not implemented yet");
}

int16_t TFT_eSPI::drawNumber(long long_num, int32_t poX, int32_t poY, uint8_t font)
{
	isDigits = true; // Eliminate jiggle in monospaced fonts
	char str[12];
	ltoa(long_num, str, 10);
	return drawString(str, poX, poY, textfont);
}

int16_t TFT_eSPI::drawNumber(long long_num, int32_t poX, int32_t poY)
{
	isDigits = true; // Eliminate jiggle in monospaced fonts
	char str[12];
	ltoa(long_num, str, 10);
	return drawString(str, poX, poY, textfont);
}

int16_t TFT_eSPI::drawFloat(float floatNumber, uint8_t decimal, int32_t x, int32_t y, uint8_t font)
{
	assert(false && "drawFloat not implemented yet");
	return 0;
}

int16_t TFT_eSPI::drawFloat(float floatNumber, uint8_t decimal, int32_t x, int32_t y)
{
	assert(false && "drawFloat not implemented yet");
	return 0;
}

/***************************************************************************************
** Function name:           drawString (with or without user defined font)
** Description :            draw string with padding if it is defined
***************************************************************************************/
// Without font number, uses font set by setTextFont()
int16_t TFT_eSPI::drawString(const String& string, int32_t poX, int32_t poY)
{
	int16_t len = string.length() + 2;
	char *buffer = new char[len];
	string.toCharArray(buffer, len);
	auto ret = drawString(buffer, poX, poY, textfont);
	delete [] buffer;
	return ret;
}
// With font number
int16_t TFT_eSPI::drawString(const String& string, int32_t poX, int32_t poY, uint8_t font)
{
	int16_t len = string.length() + 2;
	char *buffer = new char[len];
	string.toCharArray(buffer, len);
	auto ret = drawString(buffer, poX, poY, font);
	delete [] buffer;
	return ret;
}

// Without font number, uses font set by setTextFont()
int16_t TFT_eSPI::drawString(const char *string, int32_t poX, int32_t poY)
{
	return drawString(string, poX, poY, textfont);
}

// With font number. Note: font number is over-ridden if a smooth font is loaded
int16_t TFT_eSPI::drawString(const char *string, int32_t poX, int32_t poY, uint8_t font)
{
	int16_t sumX = 0;
	uint8_t padding = 1, baseline = 0;
	uint16_t cwidth = textWidth(string, font); // Find the pixel width of the string in the font
	uint16_t cheight = 8 * textsize;

#ifdef LOAD_GFXFF
#ifdef SMOOTH_FONT
	bool freeFont = (font == 1 && gfxFont && !fontLoaded);
#else
	bool freeFont = (font == 1 && gfxFont);
#endif

	if (freeFont) {
		cheight = glyph_ab * textsize;
		poY += cheight; // Adjust for baseline datum of free fonts
		baseline = cheight;
		padding =101; // Different padding method used for Free Fonts

		// We need to make an adjustment for the bottom of the string (eg 'y' character)
		if ((textdatum == BL_DATUM) || (textdatum == BC_DATUM) || (textdatum == BR_DATUM)) {
			cheight += glyph_bb * textsize;
		}
	}
#endif


	// If it is not font 1 (GLCD or free font) get the baseline and pixel height of the font
#ifdef SMOOTH_FONT
	if(fontLoaded) {
		baseline = gFont.maxAscent;
		cheight  = fontHeight();
	}
	else
#endif
		if (font!=1) {
			baseline = pgm_read_byte( &fontdata[font].baseline ) * textsize;
			cheight = fontHeight(font);
		}

	if (textdatum || padX) {

		switch(textdatum) {
		case TC_DATUM:
			poX -= cwidth/2;
			padding += 1;
			break;
		case TR_DATUM:
			poX -= cwidth;
			padding += 2;
			break;
		case ML_DATUM:
			poY -= cheight/2;
			//padding += 0;
			break;
		case MC_DATUM:
			poX -= cwidth/2;
			poY -= cheight/2;
			padding += 1;
			break;
		case MR_DATUM:
			poX -= cwidth;
			poY -= cheight/2;
			padding += 2;
			break;
		case BL_DATUM:
			poY -= cheight;
			//padding += 0;
			break;
		case BC_DATUM:
			poX -= cwidth/2;
			poY -= cheight;
			padding += 1;
			break;
		case BR_DATUM:
			poX -= cwidth;
			poY -= cheight;
			padding += 2;
			break;
		case L_BASELINE:
			poY -= baseline;
			//padding += 0;
			break;
		case C_BASELINE:
			poX -= cwidth/2;
			poY -= baseline;
			padding += 1;
			break;
		case R_BASELINE:
			poX -= cwidth;
			poY -= baseline;
			padding += 2;
			break;
		}
	}


	int8_t xo = 0;
#ifdef LOAD_GFXFF
	if (freeFont && (textcolor!=textbgcolor)) {
		cheight = (glyph_ab + glyph_bb) * textsize;
		// Get the offset for the first character only to allow for negative offsets
		uint16_t c2 = 0;
		uint16_t len = strlen(string);
		uint16_t n = 0;

		while (n < len && c2 == 0) c2 = decodeUTF8((uint8_t*)string, &n, len - n);

		if((c2 >= pgm_read_word(&gfxFont->first)) && (c2 <= pgm_read_word(&gfxFont->last) )) {
			c2 -= pgm_read_word(&gfxFont->first);
			GFXglyph *glyph = &(((GFXglyph *)pgm_read_dword(&gfxFont->glyph))[c2]);
			xo = pgm_read_byte(&glyph->xOffset) * textsize;
			// Adjust for negative xOffset
			if (xo > 0) xo = 0;
			else cwidth -= xo;
			// Add 1 pixel of padding all round
			//cheight +=2;
			//fillRect(poX+xo-1, poY - 1 - glyph_ab * textsize, cwidth+2, cheight, textbgcolor);
			fillRect(poX+xo, poY - glyph_ab * textsize, cwidth, cheight, textbgcolor);
		}
		padding -=100;
	}
#endif

	uint16_t len = strlen(string);
	uint16_t n = 0;

#ifdef SMOOTH_FONT
	if(fontLoaded) {
		setCursor(poX, poY);

		bool fillbg = _fillbg;
		// If padding is requested then fill the text background
		if (padX && !_fillbg) _fillbg = true;

		while (n < len) {
			uint16_t uniCode = decodeUTF8((uint8_t*)string, &n, len - n);
			drawGlyph(uniCode);
		}
		_fillbg = fillbg; // restore state
		sumX += cwidth;
		//fontFile.close();
	}
	else
#endif
	{
		while (n < len) {
			uint16_t uniCode = decodeUTF8((uint8_t*)string, &n, len - n);
			sumX += drawChar(uniCode, poX+sumX, poY, font);
		}
	}

	//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv DEBUG vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
	// Switch on debugging for the padding areas
	//#define PADDING_DEBUG

#ifndef PADDING_DEBUG
	//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ DEBUG ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

	if((padX>cwidth) && (textcolor!=textbgcolor)) {
		int16_t padXc = poX+cwidth+xo;
#ifdef LOAD_GFXFF
		if (freeFont) {
			poX +=xo; // Adjust for negative offset start character
			poY -= glyph_ab * textsize;
			sumX += poX;
		}
#endif
		switch(padding) {
		case 1:
			fillRect(padXc,poY,padX-cwidth,cheight, textbgcolor);
			break;
		case 2:
			fillRect(padXc,poY,(padX-cwidth)>>1,cheight, textbgcolor);
			padXc = poX - ((padX-cwidth)>>1);
			fillRect(padXc,poY,(padX-cwidth)>>1,cheight, textbgcolor);
			break;
		case 3:
			if (padXc>padX) padXc = padX;
			fillRect(poX + cwidth - padXc,poY,padXc-cwidth,cheight, textbgcolor);
			break;
		}
	}


#else

	//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv DEBUG vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
	// This is debug code to show text (green box) and blanked (white box) areas
	// It shows that the padding areas are being correctly sized and positioned

	if((padX>sumX) && (textcolor!=textbgcolor)) {
		int16_t padXc = poX+sumX; // Maximum left side padding
#ifdef LOAD_GFXFF
		if ((font == 1) && (gfxFont)) poY -= glyph_ab;
#endif
		drawRect(poX,poY,sumX,cheight, TFT_GREEN);
		switch(padding) {
		case 1:
			drawRect(padXc,poY,padX-sumX,cheight, TFT_WHITE);
			break;
		case 2:
			drawRect(padXc,poY,(padX-sumX)>>1, cheight, TFT_WHITE);
			padXc = (padX-sumX)>>1;
			drawRect(poX - padXc,poY,(padX-sumX)>>1,cheight, TFT_WHITE);
			break;
		case 3:
			if (padXc>padX) padXc = padX;
			drawRect(poX + sumX - padXc,poY,padXc-sumX,cheight, TFT_WHITE);
			break;
		}
	}
#endif
	//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ DEBUG ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

	return sumX;
}


/***************************************************************************************
** Function name:           drawCentreString (deprecated, use setTextDatum())
** Descriptions:            draw string centred on dX
***************************************************************************************/
int16_t TFT_eSPI::drawCentreString(const String& string, int32_t dX, int32_t poY, uint8_t font)
{
	int16_t len = string.length() + 2;
	char *buffer = new char[len];
	string.toCharArray(buffer, len);
	auto ret = drawCentreString(buffer, dX, poY, font);
	delete [] buffer;
	return ret;
}

int16_t TFT_eSPI::drawCentreString(const char *string, int32_t dX, int32_t poY, uint8_t font)
{
	uint8_t tempdatum = textdatum;
	int32_t sumX = 0;
	textdatum = TC_DATUM;
	sumX = drawString(string, dX, poY, font);
	textdatum = tempdatum;
	return sumX;
}


/***************************************************************************************
** Function name:           drawRightString (deprecated, use setTextDatum())
** Descriptions:            draw string right justified to dX
***************************************************************************************/
int16_t TFT_eSPI::drawRightString(const String& string, int32_t dX, int32_t poY, uint8_t font)
{
	int16_t len = string.length() + 2;
	char *buffer = new char[len];
	string.toCharArray(buffer, len);
	auto ret = drawRightString(buffer, dX, poY, font);
	delete [] buffer;
	return ret;
}

int16_t TFT_eSPI::drawRightString(const char *string, int32_t dX, int32_t poY, uint8_t font)
{
	uint8_t tempdatum = textdatum;
	int16_t sumX = 0;
	textdatum = TR_DATUM;
	sumX = drawString(string, dX, poY, font);
	textdatum = tempdatum;
	return sumX;
}

void TFT_eSPI::setCursor(int16_t x, int16_t y)
{
	assert(false && "setCursor not implemented yet");

}

void TFT_eSPI::setCursor(int16_t x, int16_t y, uint8_t font)
{
	assert(false && "setCursor not implemented yet");
}

int16_t TFT_eSPI::getCursorX(void)
{
	assert(false && "getCursorX not implemented yet");
	return 0;
}

int16_t TFT_eSPI::getCursorY(void)
{
	assert(false && "getCursorY not implemented yet");
	return 0;
}

void TFT_eSPI::setTextColor(uint16_t c)
{
	// For 'transparent' background, we'll set the bg
	// to the same as fg instead of using a flag
	textcolor = textbgcolor = c;
}

void TFT_eSPI::setTextColor(uint16_t c, uint16_t b, bool bgfill)
{
	textcolor   = c;
	textbgcolor = b;
	_fillbg     = bgfill;
}

void TFT_eSPI::setTextSize(uint8_t s)
{
	if (s>7) s = 7; // Limit the maximum size multiplier so byte variables can be used for rendering
	textsize = (s > 0) ? s : 1; // Don't allow font size 0
}

void TFT_eSPI::setTextWrap(bool wrapX, bool wrapY)
{
	assert(false && "setTextWrap not implemented yet");
}

void TFT_eSPI::setTextDatum(uint8_t datum)
{
	assert(false && "setTextDatum not implemented yet");
}

uint8_t TFT_eSPI::getTextDatum()
{
	assert(false && "getTextDatum not implemented yet");
	return 0;
}

void TFT_eSPI::setTextPadding(uint16_t x_width)
{
	assert(false && "setTextPadding not implemented yet");
}

uint16_t TFT_eSPI::getTextPadding()
{
	assert(false && "getTextPadding not implemented yet");
	return 0;
}

void TFT_eSPI::setFreeFont(uint8_t font)
{
	assert(false && "setFreeFont not implemented yet");
}

void TFT_eSPI::setTextFont(uint8_t font)
{
	assert(false && "setTextFont not implemented yet");
}

int16_t TFT_eSPI::textWidth(const char *string, uint8_t font)
{
	int32_t str_width = 0;
	uint16_t uniCode  = 0;

#ifdef SMOOTH_FONT
	if(fontLoaded) {
		while (*string) {
			uniCode = decodeUTF8(*string++);
			if (uniCode) {
				if (uniCode == 0x20) str_width += gFont.spaceWidth;
				else {
					uint16_t gNum = 0;
					bool found = getUnicodeIndex(uniCode, &gNum);
					if (found) {
						if(str_width == 0 && gdX[gNum] < 0) str_width -= gdX[gNum];
						if (*string || isDigits) str_width += gxAdvance[gNum];
						else str_width += (gdX[gNum] + gWidth[gNum]);
					}
					else str_width += gFont.spaceWidth + 1;
				}
			}
		}
		isDigits = false;
		return str_width;
	}
#endif

	if (font>1 && font<9) {
		char *widthtable = (char *)pgm_read_dword( &(fontdata[font].widthtbl ) ) - 32; //subtract the 32 outside the loop

		while (*string) {
			uniCode = *(string++);
			if (uniCode > 31 && uniCode < 128)
				str_width += pgm_read_byte( widthtable + uniCode); // Normally we need to subtract 32 from uniCode
			else str_width += pgm_read_byte( widthtable + 32); // Set illegal character = space width
		}

	}
	else {

#ifdef LOAD_GFXFF
		if(gfxFont) { // New font
			while (*string) {
				uniCode = decodeUTF8(*string++);
				if ((uniCode >= pgm_read_word(&gfxFont->first)) && (uniCode <= pgm_read_word(&gfxFont->last ))) {
					uniCode -= pgm_read_word(&gfxFont->first);
					GFXglyph *glyph  = &(((GFXglyph *)pgm_read_dword(&gfxFont->glyph))[uniCode]);
					// If this is not the  last character or is a digit then use xAdvance
					if (*string  || isDigits) str_width += pgm_read_byte(&glyph->xAdvance);
					// Else use the offset plus width since this can be bigger than xAdvance
					else str_width += ((int8_t)pgm_read_byte(&glyph->xOffset) + pgm_read_byte(&glyph->width));
				}
			}
		}
		else
#endif
		{
#ifdef LOAD_GLCD
			while (*string++) str_width += 6;
#endif
		}
	}
	isDigits = false;
	return str_width * textsize;
}

int16_t TFT_eSPI::textWidth(const char *string)
{
	return textWidth(string, textfont);
}

int16_t TFT_eSPI::textWidth(const String& string, uint8_t font)
{
	int16_t len = string.length() + 2;
	char *buffer = new char[len];
	string.toCharArray(buffer, len);
	auto ret = textWidth(buffer, font);
	delete [] buffer;
	return ret;
}

int16_t TFT_eSPI::textWidth(const String& string)
{
	int16_t len = string.length() + 2;
	char *buffer = new char[len];
	string.toCharArray(buffer, len);
	auto ret = textWidth(buffer, textfont);
	delete [] buffer;
	return ret;
}

int16_t TFT_eSPI::fontHeight(int16_t font)
{
	assert(false && "fontHeight not implemented yet");
	return 0;
}

int16_t TFT_eSPI::fontHeight()
{
	assert(false && "fontHeight not implemented yet");
	return 0;
}

uint16_t TFT_eSPI::decodeUTF8(uint8_t *buf, uint16_t *index, uint16_t remaining)
{
	uint16_t c = buf[(*index)++];
	//Serial.print("Byte from string = 0x"); Serial.println(c, HEX);

	if (!_utf8) return c;

	// 7 bit Unicode
	if ((c & 0x80) == 0x00) return c;

	// 11 bit Unicode
	if (((c & 0xE0) == 0xC0) && (remaining > 1))
		return ((c & 0x1F)<<6) | (buf[(*index)++]&0x3F);

	// 16 bit Unicode
	if (((c & 0xF0) == 0xE0) && (remaining > 2)) {
		c = ((c & 0x0F)<<12) | ((buf[(*index)++]&0x3F)<<6);
		return  c | ((buf[(*index)++]&0x3F));
	}

	// 21 bit Unicode not supported so fall-back to extended ASCII
	// if ((c & 0xF8) == 0xF0) return c;

	return c; // fall-back to extended ASCII
}

uint16_t TFT_eSPI::decodeUTF8(uint8_t c)
{
	if (!_utf8) return c;

	// 7 bit Unicode Code Point
	if ((c & 0x80) == 0x00) {
		decoderState = 0;
		return c;
	}

	if (decoderState == 0) {
		// 11 bit Unicode Code Point
		if ((c & 0xE0) == 0xC0) {
			decoderBuffer = ((c & 0x1F)<<6);
			decoderState = 1;
			return 0;
		}
		// 16 bit Unicode Code Point
		if ((c & 0xF0) == 0xE0) {
			decoderBuffer = ((c & 0x0F)<<12);
			decoderState = 2;
			return 0;
		}
		// 21 bit Unicode  Code Point not supported so fall-back to extended ASCII
		// if ((c & 0xF8) == 0xF0) return c;
	}
	else {
		if (decoderState == 2) {
			decoderBuffer |= ((c & 0x3F)<<6);
			decoderState--;
			return 0;
		}
		else {
			decoderBuffer |= (c & 0x3F);
			decoderState = 0;
			return decoderBuffer;
		}
	}

	decoderState = 0;

	return c; // fall-back to extended ASCII
}

size_t TFT_eSPI::write(uint8_t)
{
	return 0;
}

void TFT_eSPI::setCallback(getColorCallback getCol)
{

}

uint16_t TFT_eSPI::fontsLoaded()
{
	assert(false && "fontsLoaded not implemented yet");
	return 0;
}

uint16_t TFT_eSPI::color565(uint8_t red, uint8_t green, uint8_t blue)
{
	return 0;
}

uint16_t TFT_eSPI::color8to16(uint8_t color332)
{
	return 0;
}

uint8_t TFT_eSPI::color16to8(uint16_t color565)
{
	return 0;
}

uint32_t TFT_eSPI::color16to24(uint16_t color565)
{
	return 0;
}

uint32_t TFT_eSPI::color24to16(uint32_t color888)
{
	return 0;
}

uint16_t TFT_eSPI::alphaBlend(uint8_t alpha, uint16_t fgc, uint16_t bgc)
{
	return 0;
}

uint16_t TFT_eSPI::alphaBlend(uint8_t alpha, uint16_t fgc, uint16_t bgc, uint8_t dither)
{
	return 0;
}

void TFT_eSPI::startWrite()
{

}

void TFT_eSPI::writeColor(uint16_t color, uint32_t len)
{

}

void TFT_eSPI::endWrite()
{

}

void TFT_eSPI::setAttribute(uint8_t id, uint8_t a)
{

}

uint8_t TFT_eSPI::getAttribute(uint8_t id)
{
	return 0;
}

void TFT_eSPI::getSetup(setup_t &tft_settings)
{

}

bool TFT_eSPI::verifySetupID(uint32_t id)
{
	return 0;
}

SPIClass &TFT_eSPI::getSPIinstance()
{
	static SPIClass SPI;
	return SPI;
}

void TFT_eSPI::begin_tft_write()
{

}

void TFT_eSPI::end_tft_write()
{

}

void TFT_eSPI::begin_tft_read()
{

}

void TFT_eSPI::end_tft_read()
{

}

