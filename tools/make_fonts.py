#!/usr/bin/env python3
"""Regenerates design/fonts/generated from the pinned upstream fonts: make_fonts.py

  web/nunito-400.woff2, web/nunito-600.woff2   text for app pages (Latin, Latin-1, Latin Extended-A, punctuation)
  web/material-symbols-rounded.woff2           the icons every view names, filled
  lvgl/hi_font_<name>.c, lvgl/hi_design_fonts.h  LVGL fonts in the sizes and glyph sets the views' panels list

Run by hand after changing a view's fonts or icons; the output is committed, so building firmware needs neither
this script nor its tools: fonttools (through uvx) and lv_font_conv (through npx). The sources are downloaded once
into ~/.cache/home-idf-fonts and checked against the hashes below.
"""
import hashlib
import json
import os
import pathlib
import shutil
import subprocess
import tempfile
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parent.parent
DESIGN = ROOT / "design"
OUT = DESIGN / "fonts" / "generated"
CACHE = pathlib.Path.home() / ".cache" / "home-idf-fonts"

FONTTOOLS = ["uvx", "--from", "fonttools[woff]==4.60.1", "fonttools"]
LV_FONT_CONV = ["npx", "--yes", "lv_font_conv@1.5.3"]

SOURCES = {
    "nunito": ("https://github.com/google/fonts/raw/604936664fd62c14271209b51f98e7f495dd1a3e/ofl/nunito/"
               "Nunito%5Bwght%5D.ttf",
               "bb55a5ca5c2042335b3991af27c4d0705d0ef41cac6164ac737fd8f2a1e85207"),
    "symbols": ("https://github.com/google/material-design-icons/raw/10ecc14790612ca8fd164e3f4ffe9c16fc50d5b7/"
                "variablefont/MaterialSymbolsRounded%5BFILL,GRAD,opsz,wght%5D.ttf",
                "7e74e36533ccee12c2b81e75bfcf3ccf123d3bb7e7e46d65d94445f23db0c97c"),
}

# Pages: English and Polish text, numbers and the usual punctuation
WEB_TEXT = "U+0020-007E,U+00A0-017F,U+2010-2027,U+2030-203A,U+2190-2193,U+2212"


def fetch(name):
    url, sha = SOURCES[name]
    path = CACHE / f"{name}-{sha[:12]}.ttf"
    if not path.exists():
        CACHE.mkdir(parents=True, exist_ok=True)
        print(f"downloading {url}")
        data = urllib.request.urlopen(url, timeout=120).read()
        if hashlib.sha256(data).hexdigest() != sha:
            raise SystemExit(f"make_fonts.py: {name} does not match its pinned sha256")
        path.write_bytes(data)
    return path


def run(cmd, cwd=None):
    # A fixed SOURCE_DATE_EPOCH keeps fonttools from stamping the time into head, so the output is reproducible
    subprocess.run(cmd, check=True, cwd=cwd, env={**os.environ, "SOURCE_DATE_EPOCH": "0"})


def views():
    for path in sorted((DESIGN / "views").glob("*/view.json")):
        yield json.loads(path.read_text("utf-8"))


def glyphs(spec):
    """'ASCII …' -> (ranges, symbols) for lv_font_conv."""
    ranges, symbols = [], spec
    if symbols.startswith("ASCII"):
        ranges.append("0x20-0x7E")
        symbols = symbols[len("ASCII"):].strip()
    return ranges, symbols


def main():
    for tool in ("uvx", "npx"):
        if shutil.which(tool) is None:
            raise SystemExit(f"make_fonts.py: {tool} not found")
    icons = sorted({cp for v in views() for k, cp in v.get("icons", {}).items() if not k.startswith("_")})
    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        nunito = {w: tmp / f"nunito-{w}.ttf" for w in (400, 600)}
        for w, path in nunito.items():
            run(FONTTOOLS + ["varLib.instancer", "-q", "-o", str(path), str(fetch("nunito")), f"wght={w}"])
        symbols_full = tmp / "symbols-full.ttf"
        run(FONTTOOLS + ["varLib.instancer", "-q", "-o", str(symbols_full), str(fetch("symbols")),
                         "FILL=1", "GRAD=0", "opsz=24", "wght=400"])
        symbols = tmp / "symbols.ttf"
        run(FONTTOOLS + ["subset", str(symbols_full), f"--unicodes={','.join(icons)}", f"--output-file={symbols}",
                         "--layout-features=", "--no-hinting"])

        web = OUT / "web"
        web.mkdir(parents=True, exist_ok=True)
        for w, path in nunito.items():
            run(FONTTOOLS + ["subset", str(path), f"--unicodes={WEB_TEXT}", "--flavor=woff2",
                             "--layout-features=kern,liga,tnum", f"--output-file={web / f'nunito-{w}.woff2'}"])
        run(FONTTOOLS + ["subset", str(symbols), f"--unicodes={','.join(icons)}", "--flavor=woff2",
                         "--layout-features=", f"--output-file={web / 'material-symbols-rounded.woff2'}"])

        lvgl = OUT / "lvgl"
        if lvgl.exists():
            shutil.rmtree(lvgl)
        lvgl.mkdir(parents=True)
        names = []
        for view in views():
            panel = view.get("panel", {})
            sets = panel.get("glyph_sets", {})
            view_icons = ",".join(f"0x{cp}" for k, cp in view.get("icons", {}).items() if not k.startswith("_"))
            for font in panel.get("fonts", []):
                name = f"hi_font_{font['name']}"
                if name in names:
                    continue
                # lv_font_conv writes its command line into the file: run it in tmp on relative names, so the
                # output carries no local paths and is the same on every run
                ranges, syms = glyphs(sets[font["glyphs"]])
                cmd = list(LV_FONT_CONV)
                if ranges or syms:          # an icons-only font has no text glyphs at all
                    cmd += ["--font", nunito[font["weight"]].name]
                for r in ranges:
                    cmd += ["-r", r]
                if syms:
                    cmd += ["--symbols", syms]
                if font.get("icons"):
                    cmd += ["--font", symbols.name, "-r", view_icons]
                cmd += ["--size", str(font["size"]), "--bpp", "4", "--format", "lvgl", "--no-compress",
                        "--lv-include", "lvgl.h", "--lv-font-name", name, "-o", f"{name}.c"]
                run(cmd, cwd=tmp)
                shutil.move(tmp / f"{name}.c", lvgl / f"{name}.c")
                names.append(name)
        decls = "\n".join(f"LV_FONT_DECLARE({n})" for n in names)
        (lvgl / "hi_design_fonts.h").write_text(
            "/* Generated by home-idf tools/make_fonts.py: do not edit. Nunito (OFL-1.1) and Material Symbols Rounded\n"
            " * (Apache-2.0), see design/fonts. */\n#pragma once\n\n#include \"lvgl.h\"\n\n" + decls + "\n", "utf-8")
    print(f"fonts written to {OUT}")


if __name__ == "__main__":
    main()
