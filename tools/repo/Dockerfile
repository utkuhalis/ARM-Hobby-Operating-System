# Hobby ARM OS package repository.
#
# Serves a small JSON index plus per-package manifest + binary files
# over plain HTTP, so a guest with a TCP/IP stack can fetch packages
# the same way `apt` fetches Debian .debs.
#
#   docker build -t hobby-os-repo tools/repo
#   docker run --rm -p 8080:8080 hobby-os-repo
#   curl http://localhost:8080/index.json

FROM python:3.12-alpine

WORKDIR /repo
COPY index.json    /repo/
COPY index.html    /repo/
COPY packages      /repo/packages
COPY serve.py      /repo/

EXPOSE 8080
CMD ["python", "/repo/serve.py"]
