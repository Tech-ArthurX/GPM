"""
scan_packages.py - Re-index OpenList /CloudService/plugins into packages.json.

Open-source variant:
- Lists .gpm files from an OpenList public directory.
- Downloads each package, reads zip comment metadata, computes sha256 and size.
- Writes a deduplicated packages.json without server-side key validation.
"""

import argparse
import hashlib
import json
import os
import urllib.parse
import urllib.request
import zipfile
from concurrent.futures import ThreadPoolExecutor, as_completed


DEFAULT_PACKAGES_JSON_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "packages.json")


def request_headers(token=""):
    headers = {
        "Content-Type": "application/json",
        "User-Agent": "GPM-Scanner/1.0",
    }
    if token:
        token = token.strip()
        if token.lower().startswith("bearer "):
            token = token[7:].strip()
        headers["Authorization"] = token
    return headers


def http_post_json(url, payload, timeout=15, token=""):
    data = json.dumps(payload).encode("utf-8")
    req = urllib.request.Request(
        url,
        data=data,
        headers=request_headers(token),
        method="POST",
    )
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return json.loads(resp.read().decode("utf-8"))


def http_download(url, dest_path, timeout=120):
    req = urllib.request.Request(url, headers=request_headers(), method="GET")
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        with open(dest_path, "wb") as f:
            while True:
                chunk = resp.read(64 * 1024)
                if not chunk:
                    break
                f.write(chunk)


def list_openlist_public(base_url, path="/CloudService/plugins", token="", refresh=False):
    """Returns [(name, full_path), ...] for files at the given path."""
    payload = {"path": path, "refresh": refresh, "password": ""}
    result = http_post_json(f"{base_url.rstrip('/')}/api/fs/list", payload, token=token)
    if result.get("code") != 200:
        raise RuntimeError(f"OpenList list failed: {result}")
    items = result.get("data", {}).get("content", [])
    return [(it["name"], f"{path.rstrip('/')}/{it['name']}") for it in items if it.get("name")]


def read_gpm_metadata(path):
    try:
        with zipfile.ZipFile(path, "r") as zf:
            comment = zf.comment or b""
    except Exception:
        return None
    for enc in ("utf-8", "gbk"):
        try:
            text = comment.decode(enc).lstrip("\ufeff").strip()
            data = json.loads(text)
            if isinstance(data, dict):
                return data
        except Exception:
            continue
    return None


def compute_sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(64 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def normalize_segment(text):
    t = text.strip()
    for ch in ['\\', '/', ':', '*', '?', '"', '<', '>', '|']:
        t = t.replace(ch, "_")
    return t


def format_size(size):
    value = float(size)
    for unit in ("B", "KB", "MB", "GB", "TB"):
        if value < 1024 or unit == "TB":
            if unit == "B":
                return f"{int(value)}B"
            return f"{value:.2f}{unit}"
        value /= 1024
    return f"{int(size)}B"


def build_filename(meta, size):
    name = normalize_segment(meta.get("name", ""))
    version = normalize_segment(meta.get("version", ""))
    author = normalize_segment(meta.get("author", ""))
    parts = [name, version, author]
    if size:
        parts.append(format_size(size))
    return "[" + ",".join(parts) + "].gpm"


def normalize_category(value):
    return str(value or "").strip()


def parse_version(v):
    out = []
    cur = 0
    have = False
    for c in v:
        if c.isdigit():
            cur = cur * 10 + int(c)
            have = True
        else:
            if have:
                out.append(cur)
                cur = 0
                have = False
    if have:
        out.append(cur)
    return out


def parse_int_version(v):
    pa = parse_version(v)
    out = 0
    for i, n in enumerate(pa):
        out += n * (1000 ** (len(pa) - i))
    return out


def download_and_inspect(base_url, remote_dir, name, work_dir):
    """Returns an entry dict or None if the file is not a usable .gpm."""
    if not name.lower().endswith(".gpm"):
        return None
    quoted = urllib.parse.quote(name, safe="")
    raw_url = f"{base_url.rstrip('/')}/d/{remote_dir.strip('/')}/{quoted}"
    dest = os.path.join(work_dir, name)
    try:
        http_download(raw_url, dest)
    except Exception as e:
        print(f"  download failed: {name} ({e})")
        return None
    meta = read_gpm_metadata(dest)
    if not meta:
        print(f"  skip: {name} missing metadata in zip comment")
        return None
    n = str(meta.get("name", "")).strip()
    v = str(meta.get("version", "")).strip()
    a = str(meta.get("author", "")).strip()
    if not n or not v or not a:
        print(f"  skip: {name} incomplete metadata")
        return None
    return {
        "name": n,
        "version": v,
        "author": a,
        "category": normalize_category(meta.get("category", "")),
        "description": str(meta.get("description", "")).strip(),
        "size": os.path.getsize(dest),
        "sha256": compute_sha256(dest),
        "filename": name,
        "url": raw_url,
    }


def main():
    parser = argparse.ArgumentParser(description="Rebuild packages.json from OpenList /CloudService/plugins")
    parser.add_argument("--base", default="https://share.osbox.top", help="OpenList API/download base URL")
    parser.add_argument("--path", default="/CloudService/plugins", help="Public directory to scan")
    parser.add_argument("--out", default=DEFAULT_PACKAGES_JSON_PATH, help="Output packages.json")
    parser.add_argument("--work", default="", help="Scratch directory for downloads (default: tempdir)")
    parser.add_argument("--workers", type=int, default=4)
    parser.add_argument("--token", default=os.environ.get("GPM_OPENLIST_TOKEN", ""), help="Optional OpenList token for listing")
    parser.add_argument("--refresh", action="store_true", help="Force OpenList storage refresh; requires token on some servers")
    args = parser.parse_args()

    if not args.work:
        import tempfile

        args.work = tempfile.mkdtemp(prefix="gpm-scan-")
    os.makedirs(args.work, exist_ok=True)

    print(f"Listing {args.path} on {args.base} ...")
    files = list_openlist_public(args.base, args.path, token=args.token, refresh=args.refresh)
    print(f"  found {len(files)} entries")

    gpm_files = [name for name, _ in files if name.lower().endswith(".gpm")]
    print(f"  {len(gpm_files)} .gpm candidates; downloading & inspecting in parallel...")

    entries = []
    with ThreadPoolExecutor(max_workers=args.workers) as ex:
        futures = {ex.submit(download_and_inspect, args.base, args.path, name, args.work): name for name in gpm_files}
        for fut in as_completed(futures):
            name = futures[fut]
            try:
                entry = fut.result()
            except Exception as e:
                print(f"  inspect failed: {name} ({e})")
                continue
            if entry is None:
                continue
            entries.append(entry)

    seen = set()
    deduped = []
    for entry in entries:
        key = (entry["author"], entry["name"], entry["version"])
        if key in seen:
            continue
        seen.add(key)
        deduped.append(entry)

    deduped.sort(key=lambda e: (e["author"].lower(), e["name"].lower(), -parse_int_version(e["version"])))

    tmp_out = args.out + ".tmp"
    with open(tmp_out, "w", encoding="utf-8", newline="\n") as f:
        json.dump(deduped, f, ensure_ascii=False, indent=2)
        f.write("\n")
    os.replace(tmp_out, args.out)
    print(f"Wrote {args.out} ({len(deduped)} packages)")


if __name__ == "__main__":
    main()
