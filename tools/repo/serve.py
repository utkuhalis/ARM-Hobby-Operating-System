#!/usr/bin/env python3
"""
Tiny HTTP server for the Hobby ARM OS package repository.

Routes:
  GET /index.json                         -> repo metadata
  GET /packages/<name>/manifest.json      -> per-package manifest
  GET /packages/<name>/<file>             -> any file inside a package dir

Designed to be small enough that the kernel-side HTTP client only has
to speak HTTP/1.0 + Content-Length to talk to it.
"""
import http.server
import os
import socketserver

PORT = 8080
ROOT = "/repo"


class Handler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=ROOT, **kwargs)

    def end_headers(self):
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Hobby-Repo", "1")
        super().end_headers()


def main():
    os.chdir(ROOT)
    with socketserver.TCPServer(("", PORT), Handler) as srv:
        print(f"Hobby ARM OS repo listening on :{PORT}", flush=True)
        srv.serve_forever()


if __name__ == "__main__":
    main()
