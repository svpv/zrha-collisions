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

static inline uint32_t rotl16(unsigned x, int k) { return x << k | x >> (16 - k); }
static inline uint32_t rotr16(unsigned x, int k) { return x >> k | x << (16 - k); }

// The original/naive ZrHa construction with two scaled-down SIMD registers.
// The bits in each register are mixed independently.  On average, the state
// collapses after only about 2^11 updates.
static inline void updateA(uint16_t x[2], uint16_t y[2])
{
    uint16_t mx[2], my[2];
    mx[0] = (uint8_t) x[0] * (x[0] >> 8);
    mx[1] = (uint8_t) x[1] * (x[1] >> 8);
    my[0] = (uint8_t) y[0] * (y[0] >> 8);
    my[1] = (uint8_t) y[1] * (y[1] >> 8);
    mx[0] += rotl16(x[1], 8);
    mx[1] += rotl16(x[0], 8);
    my[0] += y[1];
    my[1] += rotl16(y[0], 8);
    x[0] = mx[0];
    x[1] = mx[1];
    y[0] = my[0];
    y[1] = my[1];
}

// An improved construction: we multiply out x with y, so the state gets
// intermixed between the two registers.  The state collapses after about
// 2^25 updates.
static inline void updateB(uint16_t x[2], uint16_t y[2])
{
    uint16_t mx[2], my[2];
    mx[0] = (uint8_t) x[0] * (y[0] >> 8);
    mx[1] = (uint8_t) x[1] * (y[1] >> 8);
    my[0] = (uint8_t) y[0] * (x[0] >> 8);
    my[1] = (uint8_t) y[1] * (x[1] >> 8);
    mx[0] += rotl16(x[1], 8);
    mx[1] += rotl16(x[0], 8);
    my[0] += y[1];
    my[1] += rotl16(y[0], 8);
    x[0] = mx[0];
    x[1] = mx[1];
    y[0] = my[0];
    y[1] = my[1];
}

// Multiply out x with y in a different order, to elongate the datapath cycle.
// The state collapses after about 2^27 updates.
static inline void updateC(uint16_t x[2], uint16_t y[2])
{
    uint16_t mx[2], my[2];
    mx[0] = (uint8_t) x[0] * (y[0] >> 8);
    mx[1] = (uint8_t) x[1] * (y[1] >> 8);
    my[0] = (uint8_t) y[0] * (x[1] >> 8);
    my[1] = (uint8_t) y[1] * (x[0] >> 8);
    mx[0] += rotl16(x[1], 8);
    mx[1] += rotl16(x[0], 8);
    my[0] += y[1];
    my[1] += rotl16(y[0], 8);
    x[0] = mx[0];
    x[1] = mx[1];
    y[0] = my[0];
    y[1] = my[1];
}

static inline unsigned try(uint64_t seed, uint32_t imax,
	void (*update)(uint16_t x[2], uint16_t y[2]))
{
    uint16_t x[4];
    memcpy(x, &seed, 8);
    uint32_t i = 0;
    while (1) {
	int same;
	uint16_t a[4];
	memcpy(a, x, 8);
	update(x, x + 2);
	update(x, x + 2);
	update(x, x + 2);
	update(x, x + 2);
	same = (x[0] == a[0]) + (x[1] == a[1]) + (x[2] == a[2]) + (x[3] == a[3]);
	if (same >= 2 || i >= imax)
	    return i;
	i += 4;
    }
}

int main(int argc, char **argv)
{
    int which;
    if (argc == 1)
	which = 1;
    else {
	assert(argc == 2);
	char c = *argv[1];
	if (c == 'A')
	    which = 0;
	else if (c == 'B')
	    which = 1;
	else {
	    assert(c == 'C');
	    which = 2;
	}
    }
    while (1) {
	uint64_t seed = rand64();
	unsigned n;
	if (which == 0)
	    n = try(seed, 1<<13, updateA);
	else if (which == 1)
	    n = try(seed, 1<<28, updateB);
	else
	    n = try(seed, 1<<29, updateC);
	printf("%016lx\t%u\n", seed, n);
    }
}
