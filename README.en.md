<h1 align="center"><img src="gui/assets/glim.ico" alt="GPM" width="96" height="96"/></h1>

<p align="center">
  <a href="README.md">中文</a>&nbsp;&nbsp;&nbsp;|&nbsp;&nbsp;&nbsp;<a href="README.en.md">English</a>
</p>

GPM is a package management project for Windows PE and portable Windows tools.
This repository keeps the release-facing surface together: package delivery,
package metadata, script execution, and the language and specification docs.

## How It Works

The GPM server maintains a simple JSON package index with package names,
versions, sizes, digests, and download URLs. After updating the index, the
client downloads `.gpm` packages from those links and handles integrity checks,
signature verification, extraction, and installation locally.

This keeps the server lightweight and makes package installation reliable in
Windows PE and other portable Windows environments. See the
[`.gpm` package format](gpm/docs/package.en.md) and
[trust-chain policy](gpm/docs/trust-chain.en.md) for the detailed format and
signing rules.

## Package Index

`packages.json` is an array where each item describes one installable package:

```json
{
  "name": "7-Zip",
  "version": "24.09",
  "author": "ArthurX",
  "category": "Archive",
  "description": "File archiver",
  "size": 1623934,
  "sha256": "hex-encoded-sha256",
  "filename": "[7-Zip,24.09,ArthurX,1.55MB].gpm",
  "url": "https://example.test/packages/7zip.gpm",
  "key_id": "ArthurX/7-Zip",
  "public_key_url": "https://example.test/keys/ArthurX/7-Zip/7-Zip_public_key.pem",
  "public_key_signature_url": "https://example.test/keys/ArthurX/7-Zip/7-Zip_public_key.sig"
}
```

The minimum usable fields are `name`, `version`, `author`, and `url`.
Providing `size` and `sha256` is recommended so the client can display package
size and verify downloads.
If package signing is enabled, `key_id`, `public_key_url`, and
`public_key_signature_url` may also be provided.

## Layout

- `gpm/` - Go CLI, package install runtime, downloader, GUI WebSocket backend, and GS integration.
- `gs/` - GS interpreter/compiler module and language specification.
- `gui/` - Native Direct2D frontend.
- `server/` - Public package index server and scanner.
- `tools/` - Optional PE helper tools used by package scripts and runtime integration.
- `build.py` - root `.gpm` package builder, which updates `server/packages.json` by default.
- `gpm/docs/package.md` / `gpm/docs/package.en.md` - `.gpm` package format specification.
- `gpm/docs/trust-chain.md` / `gpm/docs/trust-chain.en.md` - key trust chain and verification flow.
- `gs/docs/style.md` / `gs/docs/style.en.md` - GS writing guide and syntax summary.
- `gs/docs/reference.md` / `gs/docs/reference.en.md` - GS language reference.
- `gs/docs/tutorial.md` / `gs/docs/tutorial.en.md` - GS tutorial.
- `gs/SPEC.md` / `gs/SPEC.en.md` - normative GS contract.
- `gpm/README.md` / `gpm/README.en.md` - GPM CLI overview.
- `server/README.md` / `server/README.en.md` - package index server overview.
- `gui/README.md` / `gui/README.en.md` - native frontend overview.
