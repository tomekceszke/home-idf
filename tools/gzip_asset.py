#!/usr/bin/env python3
"""Deterministic gzip (no name/mtime) for embedding web assets: gzip_asset.py IN OUT

HTML is minified first, conservatively: comments go and the contents of every <style> block are collapsed.
Markup and scripts are left byte for byte alone, because whitespace there can be part of the page (text nodes,
template literals) and these pages are hand-written, not generated.
"""
import gzip
import re
import sys


def minify_css(css):
    # Quoted strings and url() keep their spacing: a data URI or a content string is not ours to rewrite.
    kept = []

    def park(m):
        kept.append(m.group(0))
        return f"\0{len(kept) - 1}\0"

    css = re.sub(r"/\*.*?\*/", "", css, flags=re.S)
    css = re.sub(r"\"[^\"]*\"|'[^']*'|url\([^)]*\)", park, css)
    css = re.sub(r"\s+", " ", css)
    css = re.sub(r"\s*([{};:,>])\s*", r"\1", css)
    css = css.replace(";}", "}").strip()
    return re.sub(r"\0(\d+)\0", lambda m: kept[int(m.group(1))], css)


def minify_html(html):
    html = re.sub(r"<!--(?!\[if).*?-->", "", html, flags=re.S)
    return re.sub(r"(<style>)(.*?)(</style>)",
                  lambda m: m.group(1) + minify_css(m.group(2)) + m.group(3), html, flags=re.S)


if __name__ == "__main__":
    src, dst = sys.argv[1], sys.argv[2]
    with open(src, "rb") as f:
        data = f.read()
    if src.endswith(".html"):
        data = minify_html(data.decode("utf-8")).encode("utf-8")
    with open(dst, "wb") as f:
        f.write(gzip.compress(data, compresslevel=9, mtime=0))
