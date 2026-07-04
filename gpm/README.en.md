# GPM CLI

Chinese version: [README.md](README.md)

`gpm/` contains the package manager runtime and command-line entry point.

The CLI is responsible for:

- Reading the public package index.
- Resolving package downloads.
- Verifying `sha256` when metadata provides it.
- Expanding `.gpm` packages locally.
- Running GS install and uninstall scripts.
- Bridging to helper tools used in PE workflows.

## Install Model

GPM treats packages as offline artifacts once they are downloaded.
The CLI reads the package index, downloads `.gpm` files, and performs
verification, extraction, and script execution locally.
The server does not need to take part in installation or inspect package
contents online.

## Related docs

- `../build.py` - root `.gpm` package builder.
- `docs/package.en.md` - package archive layout and metadata.
- `docs/trust-chain.en.md` - key trust chain and verification flow.
- `../gs/docs/style.en.md` - GS syntax and writing conventions.
- `../gs/SPEC.en.md` - normative GS contract.
- `../README.en.md` - repository overview.
