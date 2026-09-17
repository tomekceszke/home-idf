#!/usr/bin/env python3
"""UI development without reflashing: serves an app page locally and forwards /api and /admin to a device.

Usage: dev_proxy.py <device-ip> --page firmware/web/app.html --name w-controller \
           [--login firmware/build/esp-idf/main/login_page/login.html] [--port 8080]
then open http://localhost:8080/. The page is chosen like on the device (login without a session, app with one) and
rendered with the app shell on every request, so edits show up on reload. /apple-touch-icon.png and
/manifest.webmanifest come from the page's directory. Host and Origin are rewritten to the device, so its guards
accept the requests. Development only: never expose this proxy.
"""
import argparse
import http.client
import http.server
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import render_page  # noqa: E402

ap = argparse.ArgumentParser()
ap.add_argument("device")
ap.add_argument("--page", required=True, type=pathlib.Path, help="app.html of the controller")
ap.add_argument("--name", required=True, help="device name, as passed to home_idf_app_page()")
ap.add_argument("--login", type=pathlib.Path,
                help="sign-in page rendered by the firmware build (default: build/esp-idf/main/login_page/login.html "
                     "next to the page's web/ directory)")
ap.add_argument("--port", type=int, default=8080)
args = ap.parse_args()
WEB = args.page.resolve().parent
LOGIN = args.login or WEB.parent / "build" / "esp-idf" / "main" / "login_page" / "login.html"
TYPES = {".png": "image/png", ".webmanifest": "application/manifest+json"}


class Proxy(http.server.BaseHTTPRequestHandler):
    def forward(self):
        length = int(self.headers.get("Content-Length") or 0)
        body = self.rfile.read(length) if length else None
        headers = {k: v for k, v in self.headers.items() if k.lower() not in ("host", "origin", "referer", "connection")}
        headers["Host"] = args.device
        if self.headers.get("Origin"):
            headers["Origin"] = f"http://{args.device}"
        conn = http.client.HTTPConnection(args.device, 80, timeout=20)
        conn.request(self.command, self.path, body=body, headers=headers)
        r = conn.getresponse()
        data = r.read()
        self.send_response(r.status)
        for k, v in r.getheaders():
            if k.lower() not in ("transfer-encoding", "connection", "content-length"):
                self.send_header(k, v)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)
        return r.status, data

    def do_GET(self):
        if self.path.startswith(("/api/", "/admin/")):
            return self.forward()
        if self.path in ("/apple-touch-icon.png", "/manifest.webmanifest"):
            f = WEB / self.path.lstrip("/")
            return self.static(f.read_bytes(), TYPES[f.suffix])
        # Same page selection as the firmware: ask the device whether the cookie is a valid session
        conn = http.client.HTTPConnection(args.device, 80, timeout=10)
        conn.request("GET", "/api/session", headers={"Host": args.device, "Cookie": self.headers.get("Cookie", "")})
        authenticated = b'"authenticated":true' in conn.getresponse().read()
        if authenticated:
            body = render_page.render(args.page.read_text("utf-8"), args.name).encode()
        elif LOGIN.exists():
            body = LOGIN.read_bytes()
        else:
            raise SystemExit(f"{LOGIN} is missing: build the firmware once, the sign-in page is rendered there")
        self.static(body, "text/html; charset=utf-8")

    def do_POST(self):
        self.forward()

    def static(self, data, ctype):
        self.send_response(200)
        self.send_header("Content-Type", ctype)
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def log_message(self, fmt, *a):
        pass


print(f"http://localhost:{args.port}/ -> {args.device}")
http.server.ThreadingHTTPServer(("127.0.0.1", args.port), Proxy).serve_forever()
