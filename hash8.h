// Copyright (c) 2020 Alexey Tourbin
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

static inline void Xor(uint16_t x[2], uint16_t a[2])
{
    x[0] ^= a[0];
    x[1] ^= a[1];
}

static inline void Add(uint16_t x[2], uint16_t a[2])
{
    x[0] += a[0];
    x[1] += a[1];
}

static inline void Sub(uint16_t x[2], uint16_t a[2])
{
    x[0] -= a[0];
    x[1] -= a[1];
}

static inline void Shuf(uint16_t x[2])
{
    uint16_t x0 = rotl16(x[0], 8);
    uint16_t x1 = rotl16(x[1], 8);
    x[0] = x1;
    x[1] = x0;
}

static inline void update(uint16_t x[2], uint16_t y[2], uint16_t dx[2], uint16_t dy[2])
{
    F0(x, dx);
    F1(y, dy);
    uint16_t mx[2], my[2];
    mx[0] = (uint8_t) x[0] * (y[0] >> 8);
    mx[1] = (uint8_t) x[1] * (y[1] >> 8);
    my[0] = (uint8_t) y[0] * (x[1] >> 8);
    my[1] = (uint8_t) y[1] * (x[0] >> 8);
    Shuf(y);
    F2(x, dy);
    F3(y, dx);
    Shuf(x);
    F4(x, mx);
    F5(y, my);
}

#define MINLEN 8

static uint64_t hash(const void *data, size_t len, uint64_t seed)
{
    uint16_t x[4], d[4];
    memcpy(&x, &seed, 8);
    const void *last8 = data + len - 8;
    while (data < last8) {
	memcpy(&d, data, 8);
	update(&x[0], &x[2], &d[0], &d[2]);
	data += 8;
    }
    memcpy(&d, last8, 8);
    update(&x[0], &x[2], &d[0], &d[2]);
    uint64_t xlen = len * UINT64_C(6364136223846793005);
    uint64_t h;
    memcpy(&h, x, 8);
    return rrmxmx(h) ^ xlen;
}