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
#include <inttypes.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/auxv.h>

static inline uint16_t rotl16(uint16_t x, int k) { return x << k | x >> (16 - k); }
static inline uint16_t rotr16(uint16_t x, int k) { return x >> k | x << (16 - k); }
static inline uint32_t rotl32(uint32_t x, int k) { return x << k | x >> (32 - k); }
static inline uint32_t rotr32(uint32_t x, int k) { return x >> k | x << (32 - k); }
static inline uint64_t rotl64(uint64_t x, int k) { return x << k | x >> (64 - k); }
static inline uint64_t rotr64(uint64_t x, int k) { return x >> k | x << (64 - k); }

// A known-good mixing step, by Pelle Evensen.
static inline uint64_t rrmxmx(uint64_t x)
{
    x ^= rotr64(x, 49) ^ rotr64(x, 24);
    x *= UINT64_C(0x9FB21C651E98DF25);
    x ^= x >> 28;
    x *= UINT64_C(0x9FB21C651E98DF25);
    x ^= x >> 28;
    return x;
}

#include INC // e.g. "hash1.h" the original construction

#include "slab.h"

// To detect collisions, these "hash entries" are sorted.
#pragma pack(push, 4)
struct he {
    uint64_t h;  // hash value
    uint32_t so; // slab offset
};
#pragma pack(pop)
static_assert(sizeof(struct he) == 12, "");

void hsort(struct he *hv, size_t n)
{
    if (n & 1)
	hv[n++] = (struct he) { UINT64_MAX, 0 };
    struct he *hw = hv + n;
    uint32_t d[8][256] = { 0, };
    for (size_t i = 0; i < n; i++) {
	void *p = &hv[i].h;
	uint32_t h0, h4;
	memcpy(&h0, p + 0, 4);
	memcpy(&h4, p + 4, 4);
	d[0][(uint8_t)(h0 >> 0*8)]++;
	d[1][(uint8_t)(h0 >> 1*8)]++;
	d[2][(uint8_t)(h0 >> 2*8)]++;
	d[3][(uint8_t)(h0 >> 3*8)]++;
	d[4][(uint8_t)(h4 >> 0*8)]++;
	d[5][(uint8_t)(h4 >> 1*8)]++;
	d[6][(uint8_t)(h4 >> 2*8)]++;
	d[7][(uint8_t)(h4 >> 3*8)]++;
    }
    uint32_t y0 = 0;
    uint32_t y1 = 0;
    uint32_t y2 = 0;
    uint32_t y3 = 0;
    uint32_t y4 = 0;
    uint32_t y5 = 0;
    uint32_t y6 = 0;
    uint32_t y7 = 0;
    for (size_t i = 0; i < 256; i++) {
	uint32_t x0 = d[0][i]; d[0][i] = y0, y0 += x0;
	uint32_t x1 = d[1][i]; d[1][i] = y1, y1 += x1;
	uint32_t x2 = d[2][i]; d[2][i] = y2, y2 += x2;
	uint32_t x3 = d[3][i]; d[3][i] = y3, y3 += x3;
	uint32_t x4 = d[4][i]; d[4][i] = y4, y4 += x4;
	uint32_t x5 = d[5][i]; d[5][i] = y5, y5 += x5;
	uint32_t x6 = d[6][i]; d[6][i] = y6, y6 += x6;
	uint32_t x7 = d[7][i]; d[7][i] = y7, y7 += x7;
    }
#define RadixLoop(D, V, W)					\
    for (size_t i = 0; i < n; i += 2) {				\
	struct he e0 = V[i+0];					\
	struct he e1 = V[i+1];					\
	size_t j0 = d[D][(uint8_t)(e0.h >> D*8)]++;		\
	size_t j1 = d[D][(uint8_t)(e1.h >> D*8)]++;		\
	W[j0] = e0;						\
	W[j1] = e1;						\
    }
    RadixLoop(0, hv, hw)
    RadixLoop(1, hw, hv)
    RadixLoop(2, hv, hw)
    RadixLoop(3, hw, hv)
    RadixLoop(4, hv, hw)
    RadixLoop(5, hw, hv)
    RadixLoop(6, hv, hw)
    RadixLoop(7, hw, hv)
}

// A single try: hash all strings on the slab (with a particular seed)
// and check if there are collisions.
void try(struct slab *slab, size_t n, uint64_t seed, struct he *hv)
{
    uint32_t so = 3;
    const char *s;
    uint16_t len;
    for (size_t i = 0; i < n; i++) {
	s = slab_get(slab, so);
	memcpy(&len, s - 2, 2);
	uint64_t h = hash(s, len, seed);
	hv[i] = (struct he){ h, so };
	so += len + 2;
    }
    hsort(hv, n);
    hv[n] = (struct he) { ~hv[n-1].h, 0 }; // sentinel
    for (struct he *he = hv + 1, *hend = hv + n; he < hend; ) {
	uint64_t h = he[-1].h;
	if (h != he->h) {
	    he++;
	    continue;
	}
	flockfile(stdout);
	s = slab_get(slab, he[-1].so);
	memcpy(&len, s - 2, 2);
	printf("%016" PRIx64 " %016" PRIx64 " %.*s\n", seed, h, len, s);
	do {
	    s = slab_get(slab, he->so);
	    memcpy(&len, s - 2, 2);
	    printf("%016" PRIx64 " %016" PRIx64 " %.*s\n", seed, h, len, s);
	    he++;
	} while (h == he->h);
	funlockfile(stdout);
    }
}

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

static struct {
    struct slab slab;
    uint32_t nstr;
    pthread_mutex_t mutex;
    int ntry;
    int nthr;
} G;

void *worker(void *arg)
{
    struct he *hv = arg;
    while (1) {
	// lock
	int rc = pthread_mutex_lock(&G.mutex);
	assert(rc == 0);
	// critical
	int ntry = G.ntry--;
	uint64_t seed = rand64();
	// unlock
	rc = pthread_mutex_unlock(&G.mutex);
	assert(rc == 0);
	// loop control
	if (ntry <= 0)
	    break;
	try(&G.slab, G.nstr, seed, hv);
    }
    return arg;
}

int main(int argc, char **argv)
{
#define MAXTHR 32
    G.ntry = 16;
    G.nthr = 2;

    int opt;
    while ((opt = getopt(argc, argv, "j:")) != -1)
    switch (opt) {
    case 'j':
	G.nthr = atoi(optarg);
	assert(G.nthr > 0 && G.nthr <= MAXTHR);
	break;
    default:
	assert(!!!"getopt");
    }
    if (optind < argc) {
	assert(optind + 1 == argc);
	G.ntry = atoi(argv[optind]);
	assert(G.ntry > 0);
    }
    assert(G.ntry >= G.nthr);

    slab_init(&G.slab);

    char *line = NULL;
    size_t alloc = 0;
    while (1) {
	ssize_t len = getline(&line, &alloc, stdin);
	if (len < 0)
	    break;
	assert(len > 1);
	len--;
	assert(line[len] == '\n');
	if (len < MINLEN)
	    continue;
	if (len > UINT16_MAX)
	    continue;
	slab_reserve(&G.slab, 2 + len);
	uint16_t len16 = len;
	slab_copy(&G.slab, &len16, 2);
	slab_copy(&G.slab, line, len);
	G.nstr++;
    }
    free(line);

    pthread_t tid[MAXTHR];
    pthread_mutex_init(&G.mutex, NULL);
    for (int i = 0; i < G.nthr; i++) {
	void *mem = malloc(2 * (G.nstr + 1) * sizeof(struct he));
	assert(mem);
	int rc = pthread_create(&tid[i], NULL, worker, mem);
	assert(rc == 0);
    }
    for (int i = 0; i < G.nthr; i++) {
	void *mem;
	int rc = pthread_join(tid[i], &mem);
	assert(rc == 0);
	free(mem);
    }
    return 0;
}
