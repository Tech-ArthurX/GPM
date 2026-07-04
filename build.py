#!/usr/bin/env python3
"""
Public GPM package builder.

Creates a .gpm package as a zip file with JSON metadata in the zip comment.
The open-source format does not embed signing keys or signature trailers.
"""

import argparse
import hashlib
import json
import os
import shutil
import tempfile
import urllib.parse
import urllib.request
import zipfile
from pathlib import Path


ROOT = Path(__file__).resolve().parent
DEFAULT_PACKAGES = ROOT / "server" / "packages.json"
DEFAULT_BASE_URL = "https://share.osbox.top"
DEFAULT_REMOTE_DIR = "/CloudService/plugins"
INVALID_FILENAME_CHARS = '\\/:*?"<>|'


def normalize_segment(value):
    text = str(value or "").strip()
    for ch in INVALID_FILENAME_CHARS:
        text = text.replace(ch, "_")
    return text


def format_size(size):
    value = float(size)
    for unit in ("B", "KB", "MB", "GB", "TB"):
        if value < 1024 or unit == "TB":
            if unit == "B":
                return f"{int(value)}B"
            return f"{value:.2f}{unit}"
        value /= 1024
    return f"{int(size)}B"


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def copy_source(src, core_dir):
    src = Path(src)
    if not src.exists():
        raise FileNotFoundError(src)
    if src.is_dir():
        for item in src.rglob("*"):
            if item.is_file():
                rel = item.relative_to(src)
                dest = core_dir / rel
                dest.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(item, dest)
    else:
        shutil.copy2(src, core_dir / src.name)


def download_source(url, core_dir):
    name = Path(urllib.parse.urlparse(url).path).name or "download.bin"
    dest = core_dir / name
    req = urllib.request.Request(url, headers={"User-Agent": "GPM-Builder/1.0"})
    with urllib.request.urlopen(req, timeout=180) as resp, open(dest, "wb") as f:
        shutil.copyfileobj(resp, f)
    return dest


def write_default_script(path, name, version):
    path.write_text(
        "\n".join(
            [
                f"; {name} {version} - GPM install script",
                "_PREI",
                f"  LOGS INFO, Preparing {name} {version}...",
                "",
                "_INST",
                f"  LOGS INFO, Installing {name} {version}...",
                "",
                "_POST",
                f"  LOGS INFO, {name} {version} installed.",
                "",
            ]
        ),
        encoding="utf-8",
        newline="\n",
    )


def add_tree(zf, root, prefix):
    root = Path(root)
    if not root.exists():
        return
    for item in root.rglob("*"):
        if item.is_file():
            rel = item.relative_to(root).as_posix()
            zf.write(item, f"{prefix}/{rel}")


def clean_remote_dir(remote_dir):
    remote_dir = "/" + str(remote_dir or "").strip().strip("/")
    return remote_dir if remote_dir != "/" else DEFAULT_REMOTE_DIR


def build_download_url(base_url, remote_dir, filename):
    base = base_url.rstrip("/")
    remote = clean_remote_dir(remote_dir).strip("/")
    return f"{base}/d/{remote}/{urllib.parse.quote(filename, safe='')}"


def package_filename(name, version, author, size):
    parts = [
        normalize_segment(name),
        normalize_segment(version),
        normalize_segment(author),
        format_size(size),
    ]
    return "[" + ",".join(parts) + "].gpm"


def load_packages(path):
    if not path.exists():
        return []
    text = path.read_text(encoding="utf-8").strip()
    if not text:
        return []
    data = json.loads(text)
    if not isinstance(data, list):
        raise ValueError(f"{path} must contain a JSON array")
    return data


def upsert_package(path, entry):
    path.parent.mkdir(parents=True, exist_ok=True)
    packages = load_packages(path)
    key = (
        str(entry["author"]).lower(),
        str(entry["name"]).lower(),
        str(entry["version"]).lower(),
    )
    replaced = False
    for i, item in enumerate(packages):
        item_key = (
            str(item.get("author", "")).lower(),
            str(item.get("name", "")).lower(),
            str(item.get("version", "")).lower(),
        )
        if item_key == key:
            packages[i] = entry
            replaced = True
            break
    if not replaced:
        packages.append(entry)
    tmp = path.with_suffix(path.suffix + ".tmp")
    tmp.write_text(json.dumps(packages, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
    tmp.replace(path)


def build_package(args):
    if not args.name or not args.version or not args.author:
        raise ValueError("--name, --version and --author are required")
    if not args.src and not args.url:
        raise ValueError("provide --src or --url")

    work = Path(tempfile.mkdtemp(prefix="gpm-build-"))
    try:
        core_dir = work / "core"
        scripts_dir = work / "Scripts"
        core_dir.mkdir(parents=True)
        scripts_dir.mkdir(parents=True)

        if args.src:
            copy_source(args.src, core_dir)
        if args.url:
            download_source(args.url, core_dir)

        if args.install_script:
            shutil.copy2(args.install_script, scripts_dir / "install.gs")
        else:
            write_default_script(scripts_dir / "install.gs", args.name, args.version)

        metadata = {
            "name": args.name,
            "version": args.version,
            "author": args.author,
            "category": args.category or "",
            "description": args.description or "",
        }

        package_tmp = work / "package.gpm"
        with zipfile.ZipFile(package_tmp, "w", zipfile.ZIP_DEFLATED) as zf:
            add_tree(zf, core_dir, "core")
            add_tree(zf, scripts_dir, "Scripts")
            zf.comment = json.dumps(metadata, ensure_ascii=False, separators=(",", ":")).encode("utf-8")

        size = package_tmp.stat().st_size
        filename = args.filename or package_filename(args.name, args.version, args.author, size)

        out = Path(args.out) if args.out else ROOT / filename
        if out.exists() and out.is_dir():
            out = out / filename
        elif out.suffix.lower() != ".gpm":
            out.mkdir(parents=True, exist_ok=True)
            out = out / filename
        else:
            out.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(package_tmp, out)

        final_size = out.stat().st_size
        digest = sha256_file(out)
        entry = {
            "name": args.name,
            "version": args.version,
            "author": args.author,
            "category": args.category or "",
            "description": args.description or "",
            "size": final_size,
            "sha256": digest,
            "filename": out.name,
            "url": build_download_url(args.base_url, args.remote_dir, out.name),
        }

        if not args.no_index:
            upsert_package(Path(args.packages), entry)

        print(f"created: {out}")
        print(f"size:    {final_size} bytes")
        print(f"sha256:  {digest}")
        if not args.no_index:
            print(f"index:   {args.packages}")
    finally:
        shutil.rmtree(work, ignore_errors=True)


def main():
    parser = argparse.ArgumentParser(description="Build an open-source .gpm package")
    parser.add_argument("--name", required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--author", default="ArthurX")
    parser.add_argument("--category", default="")
    parser.add_argument("--description", default="")
    parser.add_argument("--src", help="File or directory to place under core/")
    parser.add_argument("--url", help="Download a file into core/")
    parser.add_argument("--install-script", help="Custom install.gs to place under Scripts/")
    parser.add_argument("--out", default="", help="Output .gpm file or directory")
    parser.add_argument("--filename", default="", help="Override output filename")
    parser.add_argument("--packages", default=str(DEFAULT_PACKAGES), help="packages.json to update")
    parser.add_argument("--base-url", default=DEFAULT_BASE_URL)
    parser.add_argument("--remote-dir", default=DEFAULT_REMOTE_DIR)
    parser.add_argument("--no-index", action="store_true", help="Do not update packages.json")
    build_package(parser.parse_args())


if __name__ == "__main__":
    main()
