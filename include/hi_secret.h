#pragma once

/*
 * Obfuscated, NOT encrypted, credentials: hides Wi-Fi passwords, tokens and topics from a glance over the shoulder
 * at credentials.h and from `strings` on the image. Anyone with the source of this file can reverse it in seconds.
 *
 * Format: "obf1:<hex>" produced by tools/obfuscate.py. A value without the prefix is returned unchanged,
 * so plain placeholders (e.g. "" in credentials-example.h for CI) keep working.
 */

#include <stdbool.h>
#include <stddef.h>

/* Writes the clear value (NUL-terminated) into out. False when out is too small or the hex is malformed
 * (out is then an empty string). */
bool hi_secret_reveal(const char *value, char *out, size_t out_size);
