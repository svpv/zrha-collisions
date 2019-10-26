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

// This is a scaled-down version of the original ZrHa_update construction.
// Its drawback is that the mixing step is not reversible (e.g. it can be shown
// that the state deteriorates slowly as you feed zeroes into it).
static inline void update(uint32_t state[2], const void *p)
{
    uint32_t data[2];
    memcpy(&data, p, 8);
    uint32_t x0 = state[0] + data[0];
    uint32_t x1 = state[1] + data[1];
    uint32_t m0 = (uint16_t) x0 * (x0 >> 16);
    uint32_t m1 = (uint16_t) x1 * (x1 >> 16);
    // The last combining step can be either ADD or XOR.  While ADD is slightly
    // worse than XOR at being non-invertible, it combats slightly better
    // small-bit deltas (which may occur when multiplication goes wrong).
#if 1
    state[0] = m0 + rotl32(x1, 16);
    state[1] = m1 + rotl32(x0, 16);
#else
    state[0] = m0 ^ rotl32(x1, 16);
    state[1] = m1 ^ rotl32(x0, 16);
#endif
    // The second injection is a very promising way to combat faltering
    // multiplication.  (But it is even better to inject into another state.
    // This ultimately leads to an imporved constructions with 3 states.)
#if 0
    state[0] ^= data[0];
    state[1] ^= data[1];
#endif
}

// We don't handle very small inputs.
#define MINLEN 8

static uint64_t hash(const void *data, size_t len, uint64_t seed)
{
    uint32_t state[2] = { seed, seed >> 32 };
    const void *last8 = data + len - 8;
    while (data < last8) {
	update(state, data);
	data += 8;
    }
    update(state, last8);
    // Here we only study the update construction.  To produce the final
    // hash value, we resort to a known-good mixing step.
    uint64_t h = (uint64_t) state[1] << 32 | state[0];
    uint64_t xlen = len * UINT64_C(6364136223846793005);
    return rrmxmx(h) ^ xlen;
}
