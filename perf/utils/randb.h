#pragma once

#include <random>

struct RandomBytesGenerator {
	RandomBytesGenerator(unsigned min = 0, unsigned max = UCHAR_MAX) :
		gen(std::random_device()()), distr(min, max) { }
	void prebuf()
	{
		for (pos = 0; pos < sizeof(buf); pos++)
			buf[pos] = distr(gen);
	}
	unsigned char get()
	{
		if (pos == 0)
			prebuf();
		return buf[--pos];
	}
	unsigned char buf[1024];
	size_t pos = 0;
	std::mt19937_64 gen;
	std::uniform_int_distribution<unsigned short> distr;
};
