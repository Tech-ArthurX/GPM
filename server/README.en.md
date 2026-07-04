# Server

Chinese version: [README.md](README.md)

`server/` provides the public package index service and index scanning tools.

Its job is to keep `packages.json` readable, refreshable, and ready for client use.

## Public endpoints

- `GET /index`
- `GET /packages.json`
- `GET /registry.json`
- `GET /healthz`

## Input and output

- Read `.gpm` packages or an existing `packages.json` from a public directory.
- Serve public fields such as package name, version, author, size, digest, and download URL.
- The same JSON may include public-key download URLs or public-key signature URLs, but never private keys.
- Do not take on the job of online package search or package content inspection.
- Final package signature verification is performed locally by the client after download.

## Related docs

- `../README.en.md` - repository overview.
- `../build.py` - root public `.gpm` package builder.
- `../gpm/docs/package.en.md` - `.gpm` package format.
- `../gpm/docs/trust-chain.en.md` - key trust chain and local verification flow.
