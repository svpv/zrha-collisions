// Copyright (c) 2019, 2020 Alexey Tourbin
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <sys/auxv.h>

static __uint128_t rand64state;

static __attribute__((constructor)) void rand64init(void)
{
    memcpy(&rand64state, (void *) getauxval(AT_RANDOM), 16);
    rand64state |= 1;
}

static inline uint64_t rand64(void)
{
    uint64_t ret = rand64state >> 64;
    rand64state *= 0xda942042e4dd58b5;
    return ret;
}

static inline uint32_t rotl32(uint32_t x, int k) { return x << k | x >> (32 - k); }
static inline uint32_t rotr32(uint32_t x, int k) { return x >> k | x << (32 - k); }

static inline void updateA(uint32_t x[2], uint32_t y[2])
{
    uint32_t mx[2], my[2];
    mx[0] = (uint16_t) x[0] * (x[0] >> 16);
    mx[1] = (uint16_t) x[1] * (x[1] >> 16);
    my[0] = (uint16_t) y[0] * (y[0] >> 16);
    my[1] = (uint16_t) y[1] * (y[1] >> 16);
    mx[0] += rotl32(x[1], 16);
    mx[1] += rotl32(x[0], 16);
    my[0] += rotl32(y[1], 16);
    my[1] += rotl32(y[0], 16);
    x[0] = mx[0];
    x[1] = mx[1];
    y[0] = my[0];
    y[1] = my[1];
}

static inline unsigned try(__uint128_t seed,
	void (*update)(uint32_t x[2], uint32_t y[2]))
{
    uint32_t x[4];
    memcpy(x, &seed, 16);
    uint32_t i = 0;
    do {
	int same;
	uint32_t a[4];
	memcpy(a, x, 16);
	update(x, x + 2);
	update(x, x + 2);
	update(x, x + 2);
	update(x, x + 2);
	same = (x[0] == a[0]) + (x[1] == a[1]) + (x[2] == a[2]) + (x[3] == a[3]);
	if (same >= 2)
	    return i;
	i += 4;
    } while (i);
    return UINT32_MAX;
}

int main(int argc, char **argv)
{
    while (1) {
	uint64_t seed0 = rand64();
	uint64_t seed1 = rand64();
	__uint128_t seed = seed0 | (__uint128_t) seed1 << 64;
	unsigned n = try(seed, updateA);
	printf("%016lx%016lx\t%u\n", seed1, seed0, n);
    }
}
