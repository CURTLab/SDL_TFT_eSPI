#ifndef ARDUINO_H
#define ARDUINO_H

//#define _CRT_SECURE_DEPRECATE_MEMORY
#include <string.h>
#include <stdint.h>
#include <iostream>
#include <string>
#include <algorithm>
#include "WString.h"
#include "Print.h"
#include "Stream.h"

#undef min
#undef max

#define PROGMEM

#define log_i(__x__) std::cout << __x__ << std::endl;

#define pgm_read_byte(addr) (*(const uint8_t *)(addr))
#define pgm_read_word(addr) (*(const uint16_t *)(addr))
#define pgm_read_dword(addr) (*(const uint16_t *)(addr))

class SerialClass : public Stream
{
public:
	// Print interface
public:
	void begin(unsigned long speed = 115200);

	size_t write(uint8_t u);

	// Stream interface
public:
	int available() { return 0; }
	int read() { return 0; }
	int peek() { return 0; }
	void flush();
};

extern SerialClass Serial;

// WMath prototypes
long random(long);
long random(long, long);
void randomSeed(unsigned long);
long map(long, long, long, long, long);

// other
void yield();
unsigned long millis();

extern void setup();
extern void loop();

#endif // ARDUINO_H
