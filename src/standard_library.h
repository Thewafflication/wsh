/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file standard_library.h
 * @brief Immutable embedded standard-library command registry.
 */

#ifndef WSH_STANDARD_LIBRARY_H
#define WSH_STANDARD_LIBRARY_H

#include "wsh/core.h"

/** Command accepts the common optional `--into name` result destination. */
#define WSH_LIBRARY_ACCEPTS_INTO 1U
/** Command requires the common `--into name` result destination. */
#define WSH_LIBRARY_REQUIRES_INTO 2U

/** Immutable description of one embedded standard-library command. */
typedef struct wsh_library_descriptor {
    /** Canonical namespaced command. */
    const char *name;
    /** Stable human-readable invocation signature. */
    const char *signature;
    /** Stable one-line purpose. */
    const char *summary;
    /** Bitwise registry flags. */
    unsigned flags;
} wsh_library_descriptor;

/** Return the number of embedded command descriptors. */
size_t wsh_library_descriptor_count(void);

/** Return one borrowed descriptor by zero-based index, or null. */
const wsh_library_descriptor *wsh_library_descriptor_at(size_t index);

/** Find one borrowed descriptor by exact canonical name, or null. */
const wsh_library_descriptor *wsh_library_find(wsh_string_view name);

#endif
