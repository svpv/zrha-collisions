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

static inline void update(uint32_t state[2], const uint32_t data[2])
{
    uint32_t x0 = state[0] + data[0];
    uint32_t x1 = state[1] + data[1];
    uint32_t m0 = (uint16_t) x0 * (x0 >> 16);
    uint32_t m1 = (uint16_t) x1 * (x1 >> 16);
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
    return rrmxmx(h) ^ xlen;
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
    struct he *hv = malloc((arg->n + 1) * sizeof(struct he));
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
