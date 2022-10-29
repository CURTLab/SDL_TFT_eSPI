#include "Arduino.h"

#include <stdlib.h>
#include <time.h>
#include <chrono>

SerialClass Serial = SerialClass();

void SerialClass::begin(unsigned long speed)
{

}

size_t SerialClass::write(uint8_t u)
{
	auto ret = fwrite(reinterpret_cast<char*>(&u), 1, 1, stdout);
	fflush(stdout);
	return ret;
}

void SerialClass::flush()
{
	fflush(stdout);
}

void randomSeed(unsigned long seed)
{
	if (seed != 0)
		srand(seed);
	else
		srand (time(nullptr));
}

long random(long howbig)
{
	if (howbig == 0)
		return 0;
	return rand() % howbig;
}

long random(long howsmall, long howbig)
{
	if (howsmall >= howbig)
		return howsmall;
	long diff = howbig - howsmall;
	return random(diff) + howsmall;
}

long map(long x, long in_min, long in_max, long out_min, long out_max)
{
	return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

static std::chrono::time_point<std::chrono::steady_clock> _arduino_timer_start;

int main(int argv, char **argc)
{
	_arduino_timer_start = std::chrono::steady_clock::now();

	setup();

	try {
		while(true) {
			loop();
		}
	} catch (const std::exception &e) {
		std::cerr << e.what() << '\n';
		return EXIT_FAILURE;
	}
	return 0;
}

void yield()
{

}

unsigned long millis()
{
	auto end = std::chrono::steady_clock::now();
	return std::chrono::duration_cast<std::chrono::microseconds>(end - _arduino_timer_start).count();
}
