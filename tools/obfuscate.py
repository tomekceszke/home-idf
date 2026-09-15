#!/usr/bin/env python3
"""Obfuscates a credential for credentials.h (hi_secret_reveal). NOT encryption: it only hides the value from a glance.

Usage: tools/obfuscate.py            prompts for the value (not echoed), prints "obf1:..."
       tools/obfuscate.py --reveal "obf1:..."
"""
import argparse
import getpass

KEY = b"home-idf/not-a-secret/only-hides-from-a-glance"


def mix(data: bytes) -> bytes:
    return bytes(b ^ KEY[i % len(KEY)] ^ ((i * 31 + 7) & 0xFF) for i, b in enumerate(data))


def obfuscate(value: str) -> str:
    return "obf1:" + mix(value.encode()).hex() if value else ""


def reveal(value: str) -> str:
    return mix(bytes.fromhex(value[5:])).decode() if value.startswith("obf1:") else value


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--reveal")
    args = ap.parse_args()
    if args.reveal is not None:
        print(reveal(args.reveal))
    else:
        print(f'"{obfuscate(getpass.getpass("Value: "))}"')
