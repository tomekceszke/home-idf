#!/usr/bin/env python3
"""Deterministic gzip (no name/mtime) for embedding web assets: gzip_asset.py IN OUT"""
import gzip
import sys

src, dst = sys.argv[1], sys.argv[2]
with open(src, "rb") as f:
    data = f.read()
with open(dst, "wb") as f:
    f.write(gzip.compress(data, compresslevel=9, mtime=0))
