# Hobby ARM OS marketplace backend.
#
# Local:
#   docker build -t hobby-marketplace tools/marketplace
#   docker run --rm -p 8090:8080 -v $(pwd)/tools/repo:/repo \
#              -v $(pwd)/build/mkt-data:/data hobby-marketplace
#
# Railway:
#   - Create a service from this Dockerfile.
#   - Add a Volume mounted at /data so SQLite survives redeploys.
#   - (Optional) commit packages/ + index.html into /repo/.
#
FROM python:3.12-alpine

WORKDIR /app
COPY server.py /app/

# /repo is where the static .elf packages + index.html live.
# /data is where the SQLite database lives.
RUN mkdir -p /repo /data
ENV REPO_DIR=/repo
ENV DATA_DIR=/data
ENV PORT=8080

EXPOSE 8080
CMD ["python", "/app/server.py"]
