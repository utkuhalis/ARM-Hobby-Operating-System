# Hobby ARM OS Marketplace

A small Flask-less Python service backed by SQLite that the on-device
App Store talks to over plain HTTP. Two collections live side-by-side:

- **system packages** (`.elf` binaries) – served from `/repo/packages/<name>/*`
  the same way the local docker repo does it.
- **community programs** (Maker JSON blobs) – user-submitted visual
  programs, with ratings + comments, stored in SQLite.

## Local dev

```sh
docker build -t hobby-marketplace tools/marketplace
docker run --rm -p 8090:8080 \
    -v $(pwd)/tools/repo:/repo \
    -v $(pwd)/build/mkt-data:/data \
    hobby-marketplace
```

The OS-side reaches it at `10.0.2.2:8090` from inside QEMU.

## Deploy on Railway

1. `railway login` and `railway init` from this directory.
2. `railway up` to deploy. Railway picks up `Dockerfile` automatically.
3. **Important**: add a Volume mounted at `/data` so the SQLite file
   (and therefore community programs / ratings / comments) survives
   redeploys. Otherwise every deploy starts with an empty DB.
4. (Optional) commit your `packages/` + `index.html` into the image
   under `/repo/` if you want the static `.elf` collection hosted on
   the same Railway service. For just-community-programs deploys,
   leave `/repo/` empty.

The OS-side stores the marketplace base URL in `/etc/marketplace`
(plain text, e.g. `http://YOUR-NAME-production.up.railway.app`). The
Settings window has a field to change it; the App Store reads it on
each open.

## API

```
GET  /index.json
GET  /packages/<name>/manifest.json
GET  /packages/<name>/<file>

GET  /community               -> list (?q=foo to search)
POST /community               -> upload {name, author, summary, code}
GET  /community/<id>          -> fetch program (and bump downloads)
GET  /community/<id>/comments
POST /community/<id>/comments -> {author, text}
POST /community/<id>/rate     -> {stars: 1..5}
```

No authentication on purpose; this is a hobby project. If you point a
real domain at it, put it behind a CDN or basic auth.
