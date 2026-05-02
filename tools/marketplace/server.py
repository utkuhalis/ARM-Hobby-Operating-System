#!/usr/bin/env python3
"""
Hobby ARM OS marketplace backend.

A small Flask service backed by SQLite that the on-device App Store
talks to over plain HTTP. Two collections live side-by-side:

  * "system" packages (.elf binaries) -- the existing repo, served
    from /packages/<name>/* the same way the docker repo does it.
  * "community" programs (Maker JSON blobs) -- user-submitted
    visual programs, with ratings + comments.

Endpoints
---------
GET  /index.json
GET  /packages/<name>/manifest.json
GET  /packages/<name>/<file>
GET  /community               -> list community programs (search via ?q=)
POST /community               -> upload a new community program (JSON body)
GET  /community/<id>          -> fetch one program
GET  /community/<id>/comments
POST /community/<id>/comments -> body: {"author":..., "text":...}
POST /community/<id>/rate     -> body: {"stars":1..5}

This is designed to deploy on Railway (or anything with a Python
runtime + persistent volume). The SQLite file lives under DATA_DIR
which defaults to ./data; on Railway you can mount a volume there.
"""
import json
import os
import sqlite3
import time
import http.server
import socketserver
import urllib.request
import urllib.error
from urllib.parse import urlparse, parse_qs, unquote

PORT     = int(os.environ.get("PORT", "8080"))
DATA_DIR = os.environ.get("DATA_DIR", "./data")
REPO_DIR = os.environ.get("REPO_DIR", "./repo")  # the static .elf packages
DB_PATH  = os.path.join(DATA_DIR, "marketplace.db")


def db_connect():
    os.makedirs(DATA_DIR, exist_ok=True)
    con = sqlite3.connect(DB_PATH)
    con.row_factory = sqlite3.Row
    return con


def db_init():
    con = db_connect()
    cur = con.cursor()
    cur.executescript("""
        CREATE TABLE IF NOT EXISTS programs (
            id          INTEGER PRIMARY KEY AUTOINCREMENT,
            name        TEXT NOT NULL,
            author      TEXT NOT NULL,
            summary     TEXT,
            code        TEXT NOT NULL,
            created_at  INTEGER NOT NULL,
            rating_sum  INTEGER NOT NULL DEFAULT 0,
            rating_n    INTEGER NOT NULL DEFAULT 0,
            downloads   INTEGER NOT NULL DEFAULT 0
        );
        CREATE TABLE IF NOT EXISTS comments (
            id          INTEGER PRIMARY KEY AUTOINCREMENT,
            program_id  INTEGER NOT NULL,
            author      TEXT NOT NULL,
            text        TEXT NOT NULL,
            created_at  INTEGER NOT NULL,
            FOREIGN KEY (program_id) REFERENCES programs(id)
        );
    """)
    con.commit()
    con.close()


def serve_static(path, handler):
    """Serve files from REPO_DIR if they exist."""
    full = os.path.normpath(os.path.join(REPO_DIR, path.lstrip("/")))
    if not full.startswith(os.path.abspath(REPO_DIR)):
        handler.send_error(403); return True
    if os.path.isfile(full):
        with open(full, "rb") as f:
            data = f.read()
        ext = os.path.splitext(full)[1].lower()
        ct = {
            ".json": "application/json",
            ".html": "text/html; charset=utf-8",
            ".elf":  "application/octet-stream",
            ".txt":  "text/plain; charset=utf-8",
        }.get(ext, "application/octet-stream")
        handler.send_response(200)
        handler.send_header("Content-Type", ct)
        handler.send_header("Content-Length", str(len(data)))
        handler.send_header("Cache-Control", "no-store")
        handler.end_headers()
        handler.wfile.write(data)
        return True
    return False


def json_response(handler, status, obj):
    body = json.dumps(obj, ensure_ascii=False).encode()
    handler.send_response(status)
    handler.send_header("Content-Type", "application/json; charset=utf-8")
    handler.send_header("Content-Length", str(len(body)))
    handler.send_header("Cache-Control", "no-store")
    handler.send_header("X-Hobby-Marketplace", "1")
    handler.end_headers()
    handler.wfile.write(body)


def text_response(handler, status, text):
    body = text.encode()
    handler.send_response(status)
    handler.send_header("Content-Type", "text/plain; charset=utf-8")
    handler.send_header("Content-Length", str(len(body)))
    handler.end_headers()
    handler.wfile.write(body)


def read_body(handler):
    n = int(handler.headers.get("Content-Length", "0"))
    if n == 0:
        return b""
    return handler.rfile.read(n)


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        # one-line access log
        print(f"[{self.command}] {self.path} -> {fmt % args}")

    # ---- GET ----
    def do_GET(self):
        u = urlparse(self.path)
        path = u.path
        qs   = parse_qs(u.query)

        if path == "/community":
            return self._community_list(qs)
        if path.startswith("/community/"):
            rest = path[len("/community/"):]
            if rest.endswith("/comments"):
                pid = rest[:-len("/comments")]
                return self._community_comments(pid)
            return self._community_get(rest)
        if path == "/proxy":
            return self._proxy(qs)

        # Otherwise: static -> tries /repo/<path>
        if not serve_static(path, self):
            self.send_error(404)

    # ---- POST ----
    def do_POST(self):
        u = urlparse(self.path)
        path = u.path
        if path == "/community":
            return self._community_upload()
        if path.startswith("/community/") and path.endswith("/comments"):
            pid = path[len("/community/"):-len("/comments")]
            return self._community_comment(pid)
        if path.startswith("/community/") and path.endswith("/rate"):
            pid = path[len("/community/"):-len("/rate")]
            return self._community_rate(pid)
        self.send_error(404)

    # ---- handlers ----
    def _community_list(self, qs):
        q = (qs.get("q", [""])[0] or "").strip()
        con = db_connect()
        if q:
            like = f"%{q}%"
            rows = con.execute(
                "SELECT id, name, author, summary, rating_sum, rating_n, downloads "
                "FROM programs WHERE name LIKE ? OR summary LIKE ? OR author LIKE ? "
                "ORDER BY id DESC LIMIT 50",
                (like, like, like)).fetchall()
        else:
            rows = con.execute(
                "SELECT id, name, author, summary, rating_sum, rating_n, downloads "
                "FROM programs ORDER BY id DESC LIMIT 50").fetchall()
        con.close()
        out = [{
            "id":       r["id"],
            "name":     r["name"],
            "author":   r["author"],
            "summary":  r["summary"] or "",
            "rating":   round(r["rating_sum"] / r["rating_n"], 2) if r["rating_n"] else 0,
            "rating_n": r["rating_n"],
            "downloads": r["downloads"],
        } for r in rows]
        json_response(self, 200, {"programs": out})

    def _community_get(self, pid_str):
        try:    pid = int(pid_str)
        except: return self.send_error(400)
        con = db_connect()
        r = con.execute("SELECT * FROM programs WHERE id=?", (pid,)).fetchone()
        if not r:
            con.close(); return self.send_error(404)
        con.execute("UPDATE programs SET downloads = downloads + 1 WHERE id=?",
                    (pid,))
        con.commit()
        con.close()
        json_response(self, 200, {
            "id":       r["id"],
            "name":     r["name"],
            "author":   r["author"],
            "summary":  r["summary"] or "",
            "code":     r["code"],
            "rating":   round(r["rating_sum"] / r["rating_n"], 2) if r["rating_n"] else 0,
            "rating_n": r["rating_n"],
            "downloads": r["downloads"] + 1,
        })

    def _community_comments(self, pid_str):
        try:    pid = int(pid_str)
        except: return self.send_error(400)
        con = db_connect()
        rows = con.execute(
            "SELECT id, author, text, created_at FROM comments "
            "WHERE program_id=? ORDER BY id ASC LIMIT 100", (pid,)).fetchall()
        con.close()
        json_response(self, 200, {"comments": [dict(r) for r in rows]})

    def _community_upload(self):
        body = read_body(self)
        try:
            obj = json.loads(body.decode("utf-8"))
        except Exception:
            return text_response(self, 400, "bad JSON")
        name    = (obj.get("name")    or "").strip()
        author  = (obj.get("author")  or "anonymous").strip()
        summary = (obj.get("summary") or "").strip()
        code    = (obj.get("code")    or "").strip()
        if not name or not code:
            return text_response(self, 400, "name and code required")
        if len(code) > 64 * 1024:
            return text_response(self, 400, "code too large")
        con = db_connect()
        cur = con.execute(
            "INSERT INTO programs (name, author, summary, code, created_at) "
            "VALUES (?, ?, ?, ?, ?)",
            (name, author, summary, code, int(time.time())))
        pid = cur.lastrowid
        con.commit()
        con.close()
        json_response(self, 200, {"id": pid})

    def _community_comment(self, pid_str):
        try:    pid = int(pid_str)
        except: return self.send_error(400)
        body = read_body(self)
        try:
            obj = json.loads(body.decode("utf-8"))
        except Exception:
            return text_response(self, 400, "bad JSON")
        author = (obj.get("author") or "anonymous").strip()
        text   = (obj.get("text")   or "").strip()
        if not text:
            return text_response(self, 400, "text required")
        con = db_connect()
        con.execute(
            "INSERT INTO comments (program_id, author, text, created_at) "
            "VALUES (?, ?, ?, ?)",
            (pid, author, text, int(time.time())))
        con.commit()
        con.close()
        json_response(self, 200, {"ok": True})

    def _proxy(self, qs):
        """Fetch any http:// or https:// URL on behalf of the in-OS
        browser, since the kernel doesn't speak TLS. The OS already
        knows how to talk plain HTTP to us, so it just hands the URL
        here and gets the body back."""
        url = (qs.get("url", [""])[0] or "").strip()
        url = unquote(url)
        if not url:
            return text_response(self, 400, "missing ?url=")
        if not (url.startswith("http://") or url.startswith("https://")):
            return text_response(self, 400, "url must be http:// or https://")
        try:
            req = urllib.request.Request(
                url,
                headers={"User-Agent": "HobbyARM-OS-browser/1.0"})
            with urllib.request.urlopen(req, timeout=10) as r:
                ct = r.headers.get("Content-Type",
                                   "application/octet-stream")
                code = r.status
                # cap at 2 MiB so a runaway server doesn't OOM us
                data = r.read(2 * 1024 * 1024)
        except urllib.error.HTTPError as e:
            # Forward the upstream error body so the browser can show
            # the message rather than a generic failure.
            try: data = e.read(64 * 1024)
            except Exception: data = b""
            ct   = e.headers.get("Content-Type", "text/plain") if e.headers else "text/plain"
            code = e.code
        except Exception as e:
            return text_response(self, 502, f"proxy error: {e}")
        self.send_response(code)
        self.send_header("Content-Type", ct)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Hobby-Proxy", "1")
        self.end_headers()
        self.wfile.write(data)

    def _community_rate(self, pid_str):
        try:    pid = int(pid_str)
        except: return self.send_error(400)
        body = read_body(self)
        try:
            obj = json.loads(body.decode("utf-8"))
        except Exception:
            return text_response(self, 400, "bad JSON")
        try:    stars = int(obj.get("stars", 0))
        except: stars = 0
        if stars < 1 or stars > 5:
            return text_response(self, 400, "stars must be 1..5")
        con = db_connect()
        con.execute(
            "UPDATE programs SET rating_sum = rating_sum + ?, "
            "rating_n = rating_n + 1 WHERE id=?", (stars, pid))
        con.commit()
        con.close()
        json_response(self, 200, {"ok": True})


def main():
    db_init()
    print(f"marketplace listening on :{PORT}, repo={REPO_DIR}, db={DB_PATH}")
    with socketserver.ThreadingTCPServer(("", PORT), Handler) as srv:
        srv.allow_reuse_address = True
        srv.serve_forever()


if __name__ == "__main__":
    main()
