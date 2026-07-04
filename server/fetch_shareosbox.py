"""
fetch_shareosbox.py - fetch OpenList directory JSON and direct links.

This is a lightweight helper for the public share.osbox.top bucket used by GPM.
It does not download package bodies. It only lists the directory, normalizes the
result, and writes:
  - a JSON manifest with direct download URLs
  - a plain text file with one download URL per line

Usage:
    python fetch_shareosbox.py
    python fetch_shareosbox.py --base https://share.osbox.top --path /CloudService/plugins

Environment:
    GPM_OPENLIST_TOKEN  optional OpenList token used for refresh/listing
"""

from __future__ import annotations

import argparse
import datetime as _dt
import json
import os
import sys
import urllib.parse
import urllib.request
from collections import deque


DEFAULT_BASE_URL = "https://share.osbox.top"
DEFAULT_REMOTE_DIR = "/CloudService/plugins"
DEFAULT_JSON_OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "shareosbox_files.json")
DEFAULT_LINKS_OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "shareosbox_links.txt")


def request_headers(token: str = "") -> dict[str, str]:
    headers = {
        "Content-Type": "application/json",
        "User-Agent": "GPM-ShareOSBox-Fetcher/1.0",
    }
    token = token.strip()
    if token:
        if token.lower().startswith("bearer "):
            token = token[7:].strip()
        headers["Authorization"] = token
    return headers


def http_post_json(url: str, payload: dict, timeout: int = 20, token: str = "") -> dict:
    data = json.dumps(payload).encode("utf-8")
    req = urllib.request.Request(
        url,
        data=data,
        headers=request_headers(token),
        method="POST",
    )
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return json.loads(resp.read().decode("utf-8"))


def normalize_dir(path: str) -> str:
    path = (path or "").strip().replace("\\", "/")
    if not path.startswith("/"):
        path = "/" + path
    return path.rstrip("/") or "/"


def normalize_segment(text: str) -> str:
    return urllib.parse.quote(text, safe="")


def build_download_url(base_url: str, full_path: str, name: str) -> str:
    base_url = base_url.rstrip("/")
    full_path = normalize_dir(full_path).strip("/")
    return f"{base_url}/d/{full_path}/{normalize_segment(name)}"


def list_openlist_dir(base_url: str, path: str, token: str = "", refresh: bool = False) -> list[dict]:
    payload = {"path": path, "refresh": refresh, "password": ""}
    result = http_post_json(f"{base_url.rstrip('/')}/api/fs/list", payload, token=token)
    if result.get("code") != 200:
        raise RuntimeError(f"OpenList list failed: {result}")
    return result.get("data", {}).get("content", []) or []


def collect_entries(base_url: str, root_path: str, token: str = "", refresh: bool = False, recursive: bool = False) -> list[dict]:
    root_path = normalize_dir(root_path)
    queue = deque([(root_path, refresh)])
    out: list[dict] = []

    while queue:
        current_path, do_refresh = queue.popleft()
        items = list_openlist_dir(base_url, current_path, token=token, refresh=do_refresh)
        for item in items:
            name = str(item.get("name", "")).strip()
            if not name:
                continue
            is_dir = bool(item.get("is_dir"))
            full_path = normalize_dir(f"{current_path}/{name}") if current_path != "/" else f"/{name}"
            entry = {
                "name": name,
                "path": full_path,
                "is_dir": is_dir,
                "size": item.get("size", 0),
                "modified": item.get("modified", ""),
                "created": item.get("created", ""),
                "type": item.get("type", None),
                "download_url": "" if is_dir else build_download_url(base_url, current_path, name),
            }
            out.append(entry)
            if recursive and is_dir:
                queue.append((full_path, False))

    return out


def write_json(path: str, payload: dict) -> None:
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8", newline="\n") as f:
        json.dump(payload, f, ensure_ascii=False, indent=2)
        f.write("\n")
    os.replace(tmp, path)


def write_links(path: str, entries: list[dict], gpm_only: bool = True) -> None:
    urls = []
    for entry in entries:
        if entry.get("is_dir"):
            continue
        if gpm_only and not str(entry.get("name", "")).lower().endswith(".gpm"):
            continue
        url = str(entry.get("download_url", "")).strip()
        if url:
            urls.append(url)

    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8", newline="\n") as f:
        for url in urls:
            f.write(url)
            f.write("\n")
    os.replace(tmp, path)


def main() -> int:
    parser = argparse.ArgumentParser(description="Fetch share.osbox.top JSON manifest and direct links")
    parser.add_argument("--base", default=DEFAULT_BASE_URL, help="OpenList base URL")
    parser.add_argument("--path", default=DEFAULT_REMOTE_DIR, help="Remote directory to scan")
    parser.add_argument("--json-out", default=DEFAULT_JSON_OUT, help="JSON manifest output path")
    parser.add_argument("--links-out", default=DEFAULT_LINKS_OUT, help="Plain text links output path")
    parser.add_argument("--token", default=os.environ.get("GPM_OPENLIST_TOKEN", ""), help="Optional OpenList token")
    parser.add_argument("--refresh", action="store_true", help="Force OpenList refresh for the root listing")
    parser.add_argument("--recursive", action="store_true", help="Recurse into subdirectories")
    parser.add_argument("--all-files", action="store_true", help="Include non-.gpm files in the links output")
    args = parser.parse_args()

    entries = collect_entries(args.base, args.path, token=args.token, refresh=args.refresh, recursive=args.recursive)
    files = [e for e in entries if not e.get("is_dir")]
    gpm_files = [e for e in files if str(e.get("name", "")).lower().endswith(".gpm")]

    manifest = {
        "base": args.base.rstrip("/"),
        "path": normalize_dir(args.path),
        "generated_at": _dt.datetime.now(_dt.UTC).replace(microsecond=0).isoformat().replace("+00:00", "Z"),
        "total_entries": len(entries),
        "total_files": len(files),
        "total_gpm_files": len(gpm_files),
        "entries": entries,
    }

    os.makedirs(os.path.dirname(os.path.abspath(args.json_out)), exist_ok=True)
    os.makedirs(os.path.dirname(os.path.abspath(args.links_out)), exist_ok=True)
    write_json(args.json_out, manifest)
    write_links(args.links_out, entries, gpm_only=not args.all_files)

    print(f"Wrote JSON:  {args.json_out}")
    print(f"Wrote links: {args.links_out}")
    print(f"Entries: {len(entries)}  Files: {len(files)}  GPM: {len(gpm_files)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
