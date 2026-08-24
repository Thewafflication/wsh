/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file sha256.h
 * @brief Small allocator-free SHA-256 primitive for the embedded library.
 */

#ifndef WSH_SHA256_H
#define WSH_SHA256_H

#include <stddef.h>
#include <stdint.h>

/** Incremental SHA-256 state. */
typedef struct wsh_sha256 {
    /** Current chaining words. */
    uint32_t state[8];
    /** Partial 512-bit block. */
    unsigned char block[64];
    /** Total input length in bytes. */
    uint64_t total;
    /** Initialized bytes in block. */
    size_t used;
} wsh_sha256;

/** Initialize a SHA-256 state. */
void wsh_sha256_initialize(wsh_sha256 *state);

/** Add exact bytes to a SHA-256 state. */
void wsh_sha256_update(
    wsh_sha256 *state,
    const unsigned char *bytes,
    size_t length);

/** Finalize a SHA-256 state into exactly 32 bytes. */
void wsh_sha256_finish(wsh_sha256 *state, unsigned char digest[32]);

#endif
