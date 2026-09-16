#!/usr/bin/env python3
"""Renders an app page with the shared shell: render_page.py IN OUT --name <device name>

Placeholders in the page:
  @HI_APP_NAME@      device name ("w-controller")
  @HI_APP_WORDMARK@  wordmark split on the first hyphen, like the sign-in page (accent "W-" + "controller")
  @HI_APP_CSS@       web/app_shell.css (put it first inside <style>, the page's own rules follow)
  @HI_APP_JS@        web/app_shell.js (put it first inside <script>, it defines `$` and `hi`)
  @HI_APP_TABS@      tab bar Live / History / Settings

Used at build time by home_idf_app_page() and by the local development servers, so both serve the same page.
"""
import argparse
import html
import pathlib
import re

WEB = pathlib.Path(__file__).resolve().parent.parent / "web"

TABS = """<nav class="tabs" aria-label="Sections">
  <button type="button" data-tab="live"><svg viewBox="0 0 24 24" aria-hidden="true"><circle cx="12" cy="12" r="2.5"/><path d="M7.8 7.8a6 6 0 0 0 0 8.4M16.2 7.8a6 6 0 0 1 0 8.4M4.9 4.9a10 10 0 0 0 0 14.2M19.1 4.9a10 10 0 0 1 0 14.2"/></svg>Live</button>
  <button type="button" data-tab="history"><svg viewBox="0 0 24 24" aria-hidden="true"><circle cx="12" cy="12" r="8.5"/><path d="M12 7.5V12l3 2"/></svg>History</button>
  <button type="button" data-tab="settings"><svg viewBox="0 0 24 24" aria-hidden="true"><path d="M4 7h10M18 7h2M4 17h4M12 17h8"/><circle cx="16" cy="7" r="2"/><circle cx="10" cy="17" r="2"/></svg>Settings</button>
</nav>"""


def wordmark(name):
    prefix, hyphen, rest = name.partition("-")
    if not hyphen:
        prefix, rest = name[:1], name[1:]
    e = html.escape
    return (f'<span class="mark"><span class="p">{e(prefix)}</span><span class="h">{hyphen}</span>'
            f'<span class="r">{e(rest)}</span></span>')


def render(page, name):
    values = {
        "NAME": html.escape(name),
        "WORDMARK": wordmark(name),
        "CSS": (WEB / "app_shell.css").read_text("utf-8"),
        "JS": (WEB / "app_shell.js").read_text("utf-8"),
        "TABS": TABS,
    }
    out = re.sub(r"@HI_APP_([A-Z]+)@", lambda m: values[m.group(1)], page)
    for key in ("CSS", "JS", "TABS"):
        if f"@HI_APP_{key}@" not in page:
            raise SystemExit(f"render_page.py: the page has no @HI_APP_{key}@")
    return out


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("src")
    ap.add_argument("dst")
    ap.add_argument("--name", required=True)
    args = ap.parse_args()
    dst = pathlib.Path(args.dst)
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_text(render(pathlib.Path(args.src).read_text("utf-8"), args.name), "utf-8")
