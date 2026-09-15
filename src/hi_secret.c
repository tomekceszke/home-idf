#include <string.h>

#include "hi_secret.h"

#define PREFIX "obf1:"

/* Mixed into every byte together with its position; changing it invalidates existing obfuscated values. */
static const unsigned char KEY[] = "home-idf/not-a-secret/only-hides-from-a-glance";

static int hex_val(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool hi_secret_reveal(const char *value, char *out, size_t out_size)
{
    if (out_size == 0) return false;
    out[0] = '\0';
    if (value == NULL) return true;
    size_t prefix_len = strlen(PREFIX);
    if (strncmp(value, PREFIX, prefix_len) != 0) {
        if (strlen(value) >= out_size) return false;
        strcpy(out, value);
        return true;
    }
    const char *hex = value + prefix_len;
    size_t hex_len = strlen(hex);
    size_t n = hex_len / 2;
    if (hex_len % 2 != 0 || n >= out_size) return false;
    for (size_t i = 0; i < n; i++) {
        int hi = hex_val(hex[2 * i]);
        int lo = hex_val(hex[2 * i + 1]);
        if (hi < 0 || lo < 0) {
            out[0] = '\0';
            return false;
        }
        unsigned char b = (unsigned char) ((hi << 4) | lo);
        out[i] = (char) (b ^ KEY[i % (sizeof(KEY) - 1)] ^ (unsigned char) (i * 31 + 7));
    }
    out[n] = '\0';
    return true;
}
