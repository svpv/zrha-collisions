// Copyright (c) 2019 Alexey Tourbin
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
#include <sys/auxv.h>

static inline void update(uint32_t state[2], const uint32_t data[2])
{
    uint32_t x0 = state[0] + data[0];
    uint32_t x1 = state[1] + data[1];
    uint32_t m0 = (uint16_t) x0 * (x0 >> 16);
    uint32_t m1 = (uint16_t) x1 * (x1 >> 16);
#define rotl32(x, k) (x << k | x >> (32 - k))
    state[0] = m0 + rotl32(x1, 16);
    state[1] = m1 + rotl32(x0, 16);
#if 0 // second injection
    state[0] ^= data[0];
    state[1] ^= data[1];
#endif
}

static inline void feed(uint32_t state[2], const void *data)
{
    uint32_t hunk[2];
    memcpy(&hunk, data, 8);
    update(state, hunk);
}

static inline uint64_t avalanche64(uint64_t x)
{
    x ^= x >> 33;
    x *= UINT64_C(0xff51afd7ed558ccd);
    x ^= x >> 33;
    x *= UINT64_C(0xc4ceb9fe1a85ec53);
    x ^= x >> 33;
    return x;
}

uint64_t hash(const void *data, size_t len, uint64_t seed)
{
    uint32_t state[2];
    memcpy(state, &seed, 8);
    const void *last8 = data + len - 8;
#define MINLEN 8
    assert(len >= MINLEN);
    do {
	feed(state, data);
	data += 8;
    } while (data < last8);
    feed(state, last8);
    uint64_t h = (uint64_t) state[1] << 32 | state[0];
    uint64_t xlen = len * UINT64_C(6364136223846793005);
    return avalanche64(h) ^ xlen;
}

#include "slab.h"
#include "qsort.h"

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
    struct he he;
#define HE_LESS(i, j) hv[i].h < hv[j].h
#define HE_SWAP(i, j) he = hv[i], hv[i] = hv[j], hv[j] = he
    QSORT(n, HE_LESS, HE_SWAP);
}

// A single try: hash all strings (with a particular seed) and check
// if there are collisions.
static void try(struct he *hv, size_t n, struct slab *slab, uint64_t seed)
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
	printf("%016" PRIx64 " %016" PRIx64 " %s\n",
		seed, h, (char *) slab_get(slab, he[-1].so));
	do {
	    printf("%016" PRIx64 " %016" PRIx64 " %s\n",
		    seed, h, (char *) slab_get(slab, he->so));
	    he++;
	} while (h == he->h);
    }
}

int main()
{
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
	slab_put(&slab, line, len + 1);
	n++;
    }
    free(line);

    struct he *hv = malloc((n + 1) * sizeof(struct he));
    assert(hv);

    uint64_t seed[2];
    void *auxrnd = (void *) getauxval(AT_RANDOM);
    assert(auxrnd);
    memcpy(&seed, auxrnd, 16);

#define NTRY 4
    for (int i = 0; i < NTRY; i++) {
	try(hv, n, &slab, seed[0]);
	try(hv, n, &slab, seed[1]);
	seed[0] = seed[0] * 6364136223846793005 + 1;
	seed[1] = seed[1] * 6364136223846793005 + 1;
    }
    return 0;
}
