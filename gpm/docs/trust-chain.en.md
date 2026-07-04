# GPM Trust Chain and Key Policy

Chinese version: [trust-chain.md](trust-chain.md)

This document defines `.gpm` package signing, key-pair generation, and local
verification.
The GPM server only needs to serve a JSON index and download URLs; packages are
parsed, verified, and installed locally after download.

## Trust Model

- The official root key is the trust anchor, and the root private key is kept offline by the platform.
- Intermediate keys are developer or project keys signed by the official root private key.
- Developers sign `.gpm` packages with their intermediate private keys.
- Clients embed or preload the official root public key and verify `root public key -> intermediate public key -> package content` locally.
- Intermediate keys are only valid for `usage=code_signing` and must not issue child keys.

## Root Key Generation

The root key uses Ed25519:

```bash
openssl genpkey -algorithm ED25519 -out root_private.pem
openssl pkey -in root_private.pem -pubout -out root_public.pem
```

Rules:

- `root_private.pem` stays offline and must not enter the public repository or release artifacts.
- `root_public.pem` may be embedded in the client or published with public releases.
- A root key is normally generated once; root rotation needs an explicit versioning and migration plan.

## Intermediate Key Generation

Intermediate keys should usually be generated per `developer/project`:

```bash
openssl genpkey -algorithm ED25519 -out Dism++_private_key.pem
openssl pkey -in Dism++_private_key.pem -pubout -out Dism++_public_key.pem
```

Recommended directory layout:

```text
keys/
  ArthurX/
    Dism++/
      Dism++_private_key.pem
      Dism++_public_key.pem
      Dism++_public_key.sig
```

`Dism++_private_key.pem` is given to the developer for package signing.
`Dism++_public_key.pem` and `Dism++_public_key.sig` can be referenced by the
index, embedded in the signature trailer, or published in a public-key directory.

## Intermediate Public-Key Issuance

The official root private key signs the intermediate public key.
The compatible rule is:

1. Read the intermediate public key PEM text.
2. Normalize newlines to `\n` and trim leading and trailing whitespace.
3. Append the usage label:

```text
<normalized_public_key_pem>
usage=code_signing
```

4. Compute SHA256 over that text.
5. Sign the SHA256 bytes with the root private key using Ed25519.
6. Base64-encode the signature and save it as `*_public_key.sig`.

The usage label is part of the trust boundary.
Clients must verify `usage=code_signing`; otherwise the intermediate public key
must not be accepted as a package-signing key.

## Package Signature Trailer

A signed `.gpm` package appends a `GPM2SIG0` signature trailer after the original
zip content.
Before signing, remove any old `GPM2SIG0` trailer and old 88-byte legacy Base64
signature to avoid repeated signatures.

Signing steps:

1. Read the original `.gpm` bytes.
2. Compute `fingerprint = SHA256(original_bytes)`.
3. Sign `fingerprint` with the intermediate private key using Ed25519.
4. Create the JSON payload.
5. Write `original_bytes + payload + footer`.

Payload fields:

```json
{
  "version": 2,
  "fingerprint": "base64(sha256(original_bytes))",
  "fingerprint_signature": "base64(ed25519_sign(fingerprint))",
  "signer_pubkey": "developer public key PEM",
  "signer_pubkey_signature": "base64(root_sign(sha256(pubkey + usage=code_signing)))"
}
```

The footer is 12 bytes:

```text
GPM2SIG0 + uint32_be(payload_length)
```

## Local Verification Flow

After downloading a `.gpm`, the client verifies it in this order:

1. If `packages.json` provides `sha256`, verify download integrity first.
2. Parse the final 12-byte footer and require the `GPM2SIG0` magic value.
3. Read the payload using the footer length, and treat the bytes before the payload as the original package.
4. Compute SHA256 over the original package and compare it with payload `fingerprint`.
5. Use the official root public key to verify `signer_pubkey_signature`.
6. Use `signer_pubkey` to verify `fingerprint_signature`.
7. Only then extract, read scripts, and install.

If release policy requires signed packages, packages without `GPM2SIG0` must be
rejected.
If only public-index integrity is enabled, `sha256` is still the minimum check.

## Public-Key Data In The Index

The server may maintain a single JSON file.
That JSON may include package size, digest, download URL, and public-key
locations, but it must never contain private keys.

Recommended optional fields:

```json
{
  "public_key_url": "https://example.test/keys/ArthurX/Dism++/Dism++_public_key.pem",
  "public_key_signature_url": "https://example.test/keys/ArthurX/Dism++/Dism++_public_key.sig",
  "key_id": "ArthurX/Dism++"
}
```

When the package trailer already contains `signer_pubkey` and
`signer_pubkey_signature`, the client can verify locally without fetching extra
key files.
Index key URLs are mainly useful for auditing, display, and key rotation.

## Security Requirements

- Root private keys and intermediate private keys must not be committed to the public repository.
- Private keys must not be placed in `.gpm` packages, `packages.json`, GitHub Releases, or static server directories.
- The server does not need to search package contents online or perform final package signature verification for the client.
- The client must complete hash and signature verification before extraction.
- `sha256` proves download integrity only; it is not a substitute for the root-key trust chain.
