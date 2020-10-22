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
#include <pthread.h>
#include <sys/auxv.h>

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

#if 0
#include "hash1.h" // the original construction, non-reversible mixing
#else
#include "hash2.h" // the improved construction with 2 or 3 states
#endif

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
    uint32_t so = 1;
    for (size_t i = 0; i < n; i++) {
	const char *s = slab_get(slab, so);
	size_t len = MINLEN + strlen(s + MINLEN);
	uint64_t h = hash(s, len, seed);
	hv[i] = (struct he){ h, so };
	so += len + 1;
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
	printf("%016" PRIx64 " %016" PRIx64 " %s\n",
		seed, h, (char *) slab_get(slab, he[-1].so));
	do {
	    printf("%016" PRIx64 " %016" PRIx64 " %s\n",
		    seed, h, (char *) slab_get(slab, he->so));
	    he++;
	} while (h == he->h);
	funlockfile(stdout);
    }
}

struct arg {
    struct slab *slab;
    size_t n;
    uint64_t seed;
    int ntry;
};

void *job(void *arg_)
{
    struct arg *arg = arg_;
    struct he *hv = malloc(2 * (arg->n + 1) * sizeof(struct he));
    assert(hv);

    do {
	try(arg->slab, arg->n, arg->seed, hv);
	arg->seed = arg->seed * UINT64_C(6364136223846793005) + 1;
    } while (--arg->ntry > 0);

    free(hv);
    return NULL;
}

int main(int argc, char **argv)
{
    int nlog = 6;
    if (argc > 1) {
	assert(argc == 2);
	nlog = atoi(argv[1]);
	assert(nlog > 0 && nlog < 32);
    }

    struct slab slab;
    slab_init(&slab);

    char *line = NULL;
    size_t alloc = 0;
    size_t n = 0;
    while (1) {
	ssize_t len = getline(&line, &alloc, stdin);
	if (len < 0)
	    break;
	assert(len > 1);
	len--;
	assert(line[len] == '\n');
	if (len < MINLEN)
	    continue;
	line[len] = '\0';
	uint32_t so = slab_put(&slab, line, len + 1);
	n++;
	// slab offsets limited to 4G
	if (so > (UINT32_C(63) << 26))
	    break;
    }
    free(line);

    uint64_t seed[2];
    void *auxrnd = (void *) getauxval(AT_RANDOM);
    assert(auxrnd);
    memcpy(&seed, auxrnd, 16);

    int ntry = 1 << (nlog - 1);
    struct arg arg0 = { &slab, n, seed[0], ntry };
    struct arg arg1 = { &slab, n, seed[1], ntry };
    pthread_t thread;
    int rc = pthread_create(&thread, NULL, job, &arg0);
    assert(rc == 0);
    job(&arg1);
    rc = pthread_join(thread, NULL);
    assert(rc == 0);

    return 0;
}
