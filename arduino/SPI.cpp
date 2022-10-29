#include "SPI.h"

SPIClass::SPIClass()
{

}

SPIClass::SPIClass(uint32_t mosi, uint32_t miso, uint32_t sclk, uint32_t ssel)
{

}

void SPIClass::begin(uint8_t _pin)
{

}

void SPIClass::end()
{

}

void SPIClass::beginTransaction(uint8_t pin, SPISettings settings)
{

}

void SPIClass::endTransaction(uint8_t pin)
{

}

byte SPIClass::transfer(uint8_t pin, uint8_t _data, SPITransferMode _mode)
{
	return 0;
}

uint16_t SPIClass::transfer16(uint8_t pin, uint16_t _data, SPITransferMode _mode)
{
	return 0;
}

void SPIClass::transfer(byte _pin, void *_bufout, void *_bufin, size_t _count, SPITransferMode _mode)
{

}

void SPIClass::setBitOrder(uint8_t _pin, BitOrder)
{

}

void SPIClass::setDataMode(uint8_t _pin, uint8_t)
{

}

void SPIClass::setClockDivider(uint8_t _pin, uint8_t)
{

}

void SPIClass::usingInterrupt(uint8_t interruptNumber)
{

}

void SPIClass::attachInterrupt()
{

}

void SPIClass::detachInterrupt()
{

}
