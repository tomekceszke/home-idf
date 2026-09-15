/* Host test: cc -I include src/hi_secret.c test/test_secret.c -o /tmp/t && /tmp/t "<obf1 value from tools/obfuscate.py>" "<clear>" */
#include <stdio.h>
#include <string.h>
#include "hi_secret.h"

int main(int argc, char **argv)
{
    char out[128];
    int fail = 0;
    fail |= !(hi_secret_reveal("", out, sizeof(out)) && strcmp(out, "") == 0);
    fail |= !(hi_secret_reveal("plain-text", out, sizeof(out)) && strcmp(out, "plain-text") == 0);
    fail |= hi_secret_reveal("obf1:zz", out, sizeof(out));
    fail |= hi_secret_reveal("obf1:abc", out, sizeof(out));
    fail |= hi_secret_reveal("too-long-for-the-buffer", out, 4);
    if (argc == 3) fail |= !(hi_secret_reveal(argv[1], out, sizeof(out)) && strcmp(out, argv[2]) == 0);
    printf("%s\n", fail ? "FAIL" : "ok");
    return fail;
}
