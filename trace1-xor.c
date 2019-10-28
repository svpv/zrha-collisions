#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include <assert.h>

// This is a study of one particular collision which happens under the
// scaled-down ZrHa_update construction with XOR as the last combining step.

static inline void update(uint32_t state[2], const void *p)
{
    uint32_t data[2];
    memcpy(&data, p, 8);
printf("  d0=%08x d1=%08x %.4s %.4s\n", data[0], data[1],
	(const char *) &data[0], (const char *) &data[1]);
    uint32_t x0 = state[0] + data[0];
    uint32_t x1 = state[1] + data[1];
printf("  x0=%08x x1=%08x\n", x0, x1);
    uint32_t m0 = (uint16_t) x0 * (x0 >> 16);
    uint32_t m1 = (uint16_t) x1 * (x1 >> 16);
printf("  m0=%08x m1=%08x\n", m0, m1);
#define rotl32(x, k) (x << k | x >> (32 - k))
    state[0] = m0 ^ rotl32(x1, 16);
    state[1] = m1 ^ rotl32(x0, 16);
printf("= s0=%08x s1=%08x\n", state[0], state[1]);
}

uint64_t hash(const void *data, size_t len, uint64_t seed)
{
    uint32_t state[2] = { seed, seed >> 32 };
    const void *last8 = data + len - 8;
    while (data < last8) {
	update(state, data);
	data += 8;
    }
    update(state, last8);
    // Proper mixing does not matter, the collision already happened.
    return (uint64_t) state[1] << 32 | state[0];
}

int main()
{
    const uint64_t seed = 0x6d8dc9805c6b1a71;
    const char s0[] = "_ZN13QInterfacePtrI21ClassBrowserInterfaceEC1EPS0_";
    const char s1[] = "_ZN13QInterfacePtrI21ClassBrowserInterfaceEC2EPS0_";
    uint64_t h0 = hash(s0, sizeof s0 - 1, seed);
    putchar('\n');
    uint64_t h1 = hash(s1, sizeof s1 - 1, seed);
    assert(h0 == h1);
    return 0;
}
