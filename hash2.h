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

// This is a scaled-down version of the improved ZrHa_update2 construction
// which works on two states.  The data is injected twice, and the mixing step
// is reversible.
static inline void update2(uint32_t x[2], uint32_t y[2], const void *p)
{
    uint32_t d[2];
    memcpy(d, p, 8);
    y[0] ^= d[0];
    y[1] ^= d[1];
    uint32_t m0 = (uint16_t) y[0] * (y[0] >> 16);
    uint32_t m1 = (uint16_t) y[1] * (y[1] >> 16);
    x[0] += d[0];
    x[1] += d[1];
    m0 += rotl32(x[1], 16);
    m1 += rotl32(x[0], 16);
    x[0] = m0;
    x[1] = m1;
}

// We don't handle very small inputs.
#define MINLEN 8

static uint64_t hash(const void *data, size_t len, uint64_t seed)
{
    // We have two variants: state[4] (two states) and state[6] (three states).
    // Two states are absolutely required for the update2 construction to work,
    // while three states allude to a practical implementation with 3 SIMD
    // registres.  We need the latter variant to study how to merge the states,
    // but its bigger state makes it a bit harder to find collisions.
#if 0
    uint32_t state[4] = {
	seed, seed >> 32,
	seed, seed >> 32,
    };
    const void *last8 = data + len - 8;
    if (len & 8) {
	update2(state + 2, state + 0, data + 0);
	data += 8;
    }
    while (data < last8) {
	update2(state + 0, state + 2, data + 0);
	update2(state + 2, state + 0, data + 8);
	data += 16;
    }
    update2(state + 0, state + 2, last8);
    // Here we only study the update construction.  To produce the final
    // hash value, we resort to a known-good mixing step.
    uint64_t h[2];
    memcpy(h, state, sizeof h);
    uint64_t xlen = len * UINT64_C(6364136223846793005);
    return rrmxmx(h[0]) + (rrmxmx(h[1]) ^ xlen);
#else
    uint32_t state[6] = {
	seed, seed >> 32,
	seed, seed >> 32,
	seed, seed >> 32,
    };
    const void *last8  = data + len - 8;
    const void *last16 = data + len - 16;
    const void *last24 = data + len - 24;
    while (data < last24) {
	update2(state + 0, state + 2, data + 0);
	update2(state + 2, state + 4, data + 8);
	update2(state + 4, state + 0, data + 16);
	data += 24;
    }
    if (data >= last8)
	update2(state + 0, state + 2, last8);
    else if (data >= last16) {
	update2(state + 0, state + 2, data + 0);
	update2(state + 2, state + 4, last8);
    }
    else {
	update2(state + 0, state + 2, data + 0);
	update2(state + 2, state + 4, data + 8);
	update2(state + 4, state + 0, last8);
    }
    // Here we only study the update construction.  To produce the final
    // hash value, we resort to a known-good mixing step.
    uint64_t h[3];
    memcpy(h, state, sizeof h);
    uint64_t xlen = len * UINT64_C(6364136223846793005);
    return (rrmxmx(h[0]) ^ xlen) + (rrmxmx(h[1]) ^ rrmxmx(h[2]));
#endif
}
