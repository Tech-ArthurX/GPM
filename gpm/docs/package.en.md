# .gpm Package Format

Chinese version: [package.md](package.md)

This document defines the public `.gpm` package layout.

## Container

A `.gpm` file is a zip archive.
It is treated as an installable package artifact, not as a general-purpose
application bundle.

## Required layout

The archive should contain these top-level paths:

- `core/` - payload files that will be installed.
- `Scripts/` - GS install and uninstall scripts.

Optional paths may be added when needed, but package logic should not depend on
undocumented locations.

## Metadata

Package metadata is stored as JSON in the zip comment.
The public metadata should include at least:

- `name`
- `version`
- `author`

Recommended fields:

- `category`
- `description`
- `size`
- `sha256`
- `filename`
- `key_id`
- `public_key_url`
- `public_key_signature_url`

Example:

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
  "key_id": "ArthurX/7-Zip",
  "public_key_url": "https://example.test/keys/ArthurX/7-Zip/7-Zip_public_key.pem",
  "public_key_signature_url": "https://example.test/keys/ArthurX/7-Zip/7-Zip_public_key.sig"
}
```

## Filename convention

The current public builder uses a bracketed filename convention:

```text
[Name,Version,Author,Size].gpm
```

The size segment is human-readable, for example `1.55MB`.

## Build flow

The root `../../build.py` is the public `.gpm` package builder.
It copies payload files into `core/`, copies or generates `Scripts/install.gs`,
packages the archive, writes metadata into the zip comment, and updates
`../../server/packages.json` by default.

## Validation expectations

The package host should verify only what is public and necessary:

- Archive structure.
- Metadata presence.
- `sha256` when provided by the index or local package record.
- The [trust-chain policy](trust-chain.en.md) when the package has a `GPM2SIG0` signature trailer or release policy requires signing.
- Path safety during extraction.

Server-side checks do not replace final client-side package verification; a
downloaded `.gpm` should be parsed and verified locally.
