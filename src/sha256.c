/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file sha256.c
 * @brief FIPS 180-4 SHA-256 used by fs::hash and copy verification.
 */

#include "sha256.h"

#include <string.h>

/** SHA-256 round constants. */
static const uint32_t wsh_sha256_constants[64] = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
    0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
    0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
    0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U
};

/** Rotate one 32-bit word right. */
static uint32_t wsh_sha256_rotate(uint32_t value, unsigned amount)
{
    return (value >> amount) | (value << (32U - amount));
}

/** Compress one complete SHA-256 block. */
static void wsh_sha256_compress(
    wsh_sha256 *state,
    const unsigned char block[64])
{
    uint32_t words[64];
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint32_t e;
    uint32_t f;
    uint32_t g;
    uint32_t h;
    uint32_t first;
    uint32_t second;
    size_t index;

    for (index = 0U; index < 16U; ++index) {
        words[index] = ((uint32_t)block[index * 4U] << 24U) |
            ((uint32_t)block[index * 4U + 1U] << 16U) |
            ((uint32_t)block[index * 4U + 2U] << 8U) |
            (uint32_t)block[index * 4U + 3U];
    }
    for (; index < 64U; ++index) {
        first = wsh_sha256_rotate(words[index - 15U], 7U) ^
            wsh_sha256_rotate(words[index - 15U], 18U) ^
            (words[index - 15U] >> 3U);
        second = wsh_sha256_rotate(words[index - 2U], 17U) ^
            wsh_sha256_rotate(words[index - 2U], 19U) ^
            (words[index - 2U] >> 10U);
        words[index] = words[index - 16U] + first +
            words[index - 7U] + second;
    }
    a = state->state[0];
    b = state->state[1];
    c = state->state[2];
    d = state->state[3];
    e = state->state[4];
    f = state->state[5];
    g = state->state[6];
    h = state->state[7];
    for (index = 0U; index < 64U; ++index) {
        first = h + (wsh_sha256_rotate(e, 6U) ^
            wsh_sha256_rotate(e, 11U) ^ wsh_sha256_rotate(e, 25U)) +
            ((e & f) ^ (~e & g)) + wsh_sha256_constants[index] + words[index];
        second = (wsh_sha256_rotate(a, 2U) ^
            wsh_sha256_rotate(a, 13U) ^ wsh_sha256_rotate(a, 22U)) +
            ((a & b) ^ (a & c) ^ (b & c));
        h = g;
        g = f;
        f = e;
        e = d + first;
        d = c;
        c = b;
        b = a;
        a = first + second;
    }
    state->state[0] += a;
    state->state[1] += b;
    state->state[2] += c;
    state->state[3] += d;
    state->state[4] += e;
    state->state[5] += f;
    state->state[6] += g;
    state->state[7] += h;
}

/** @brief Implements wsh_sha256_initialize. */
void wsh_sha256_initialize(wsh_sha256 *state)
{
    static const uint32_t initial[8] = {
        0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
        0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U
    };

    memset(state, 0, sizeof(*state));
    memcpy(state->state, initial, sizeof(initial));
}

/** @brief Implements wsh_sha256_update. */
void wsh_sha256_update(
    wsh_sha256 *state,
    const unsigned char *bytes,
    size_t length)
{
    size_t available;
    size_t take;

    state->total += (uint64_t)length;
    while (length != 0U) {
        available = sizeof(state->block) - state->used;
        take = length < available ? length : available;
        memcpy(state->block + state->used, bytes, take);
        state->used += take;
        bytes += take;
        length -= take;
        if (state->used == sizeof(state->block)) {
            wsh_sha256_compress(state, state->block);
            state->used = 0U;
        }
    }
}

/** @brief Implements wsh_sha256_finish. */
void wsh_sha256_finish(wsh_sha256 *state, unsigned char digest[32])
{
    uint64_t bits;
    size_t index;

    bits = state->total * 8U;
    state->block[state->used++] = 0x80U;
    if (state->used > 56U) {
        memset(state->block + state->used, 0, 64U - state->used);
        wsh_sha256_compress(state, state->block);
        state->used = 0U;
    }
    memset(state->block + state->used, 0, 56U - state->used);
    for (index = 0U; index < 8U; ++index) {
        state->block[63U - index] = (unsigned char)(bits >> (index * 8U));
    }
    wsh_sha256_compress(state, state->block);
    for (index = 0U; index < 8U; ++index) {
        digest[index * 4U] = (unsigned char)(state->state[index] >> 24U);
        digest[index * 4U + 1U] = (unsigned char)(state->state[index] >> 16U);
        digest[index * 4U + 2U] = (unsigned char)(state->state[index] >> 8U);
        digest[index * 4U + 3U] = (unsigned char)state->state[index];
    }
    memset(state, 0, sizeof(*state));
}
