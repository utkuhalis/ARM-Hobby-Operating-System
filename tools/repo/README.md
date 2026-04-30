# Hobby ARM OS Package Repository

A small HTTP file server that hosts the kernel's package catalogue.
Once the guest has a TCP/IP stack, the on-device package manager can
fetch `index.json` here, then download individual `manifest.json` +
`*.elf` payloads on demand.

## Run with Docker

```sh
docker build -t hobby-os-repo tools/repo
docker run --rm -p 8090:8080 hobby-os-repo
```

Then in another terminal:

```sh
curl http://localhost:8090/index.json
curl http://localhost:8090/packages/hello/manifest.json
```

(macOS often has Apache on :8080 already; mapping to 8090 avoids
the conflict. Inside the container the server still listens on 8080.)

## Run without Docker (Python only)

```sh
cd tools/repo
python3 serve.py
```

## Adding a package

1. Pick a name and create `packages/<name>/`.
2. Drop a `manifest.json` (see the existing ones) plus the binary
   payload (`<name>.elf`).
3. Add an entry to `index.json` that points at the manifest.
4. Optionally include `icon.txt` (8x8 ASCII), screenshot files, or a
   signed checksum.

## Manifest schema (v1)

| Field        | Required | Notes                                             |
|--------------|----------|---------------------------------------------------|
| `name`       | yes      | unique package id, lowercase                      |
| `version`    | yes      | semver-ish                                        |
| `summary`    | yes      | one-line description shown in the App Store      |
| `description`| no       | longer prose, optional                            |
| `license`    | yes      | "MIT", "Apache-2.0", "Proprietary", ...           |
| `open_source`| yes      | true / false; closed-source binaries are allowed  |
| `author`     | yes      | display name                                      |
| `homepage`   | no       | URL                                               |
| `exec`       | yes      | repo-relative path to the AArch64 ELF binary      |
| `sha256`     | yes      | hex digest, verified by the kernel after download |
| `size`       | no       | bytes                                             |
| `icon`       | no       | repo-relative path to an icon                     |
| `depends`    | no       | list of package names or virtual capabilities     |
| `syscalls`   | no       | enumerated syscalls used by this binary           |
