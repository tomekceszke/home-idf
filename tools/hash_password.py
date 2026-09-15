#!/usr/bin/env python3
"""Generate AUTH_PASSWORD_* defines for main/config/credentials.h.

Usage: tools/hash_password.py [--iterations N]      (prompts for the password, not echoed)
The device verifies with mbedtls PBKDF2-HMAC-SHA256, 32-byte key, same salt and iterations.
"""
import argparse
import getpass
import hashlib
import os
import sys

ap = argparse.ArgumentParser()
ap.add_argument("--iterations", type=int, default=20000)
args = ap.parse_args()

pw = getpass.getpass("Password: ")
if len(pw) < 10:
    sys.exit("use at least 10 characters")
if getpass.getpass("Repeat: ") != pw:
    sys.exit("passwords differ")

salt = os.urandom(16)
key = hashlib.pbkdf2_hmac("sha256", pw.encode(), salt, args.iterations, 32)
print(f'#define AUTH_PASSWORD_ITERATIONS    {args.iterations}')
print(f'#define AUTH_PASSWORD_SALT_HEX      "{salt.hex()}"')
print(f'#define AUTH_PASSWORD_HASH_HEX      "{key.hex()}"')
