#!/usr/bin/env python3
"""gen_sbom.py — generate CycloneDX 1.5 SBOMs for olv-backend and olv-frontend.

stdlib-only (air-gap friendly; no pip install required). Writes
sbom/backend.cdx.json and sbom/frontend.cdx.json by default, creating the
output directory if needed. Also prints a one-line license summary per BOM.

Usage:
  python3 scripts/gen_sbom.py [--out DIR] [--print]

  --out DIR   output directory for the two *.cdx.json files (default: sbom/)
  --print     print the generated BOMs to stdout instead of writing files

See docs/SBOM.md for the full strategy (what's in each BOM, how to feed them
to scanners, when to regenerate).
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import sys
import uuid
from datetime import datetime, timezone

OLV_VERSION = "0.1.0"

# Vendored frontend image assets (frontend/assets/), added to the frontend
# BOM as CycloneDX "file" components. `version` here is the imagery vintage
# (the NASA product's acquisition/composite date), not a software release —
# there is no other natural version axis for a static image. Paths are
# relative to the repo root, matching how CycloneDX component "name" is
# conventionally rendered for file-type components.
_FRONTEND_ASSETS = [
    {
        "path": "frontend/assets/earth_day.jpg",
        "version": "2004.12",  # NASA Blue Marble: Next Generation, Dec 2004 composite
        "product": "NASA Blue Marble: Next Generation",
        "source_url": (
            "https://eoimages.gsfc.nasa.gov/images/imagerecords/73000/73909/"
            "world.topo.bathy.200412.3x5400x2700.jpg"
        ),
    },
    {
        "path": "frontend/assets/earth_night.jpg",
        "version": "2012",  # Suomi NPP VIIRS "Earth at Night 2012"
        "product": 'NASA Black Marble / Suomi NPP VIIRS -- "Earth at Night 2012"',
        "source_url": (
            "https://eoimages.gsfc.nasa.gov/images/imagerecords/79000/79765/"
            "dnb_land_ocean_ice.2012.3600x1800.jpg"
        ),
    },
]

# NASA imagery license: US-government-produced, generally not subject to
# copyright; not an SPDX-identified license, so this uses license.name (not
# license.id) per the CycloneDX schema.
_ASSET_LICENSE = {
    "license": {
        "name": "Public domain (NASA Media Usage Guidelines)",
        "url": "https://www.nasa.gov/nasa-brand-center/images-and-media/",
    }
}

# Fixed namespace UUID for this project's deterministic serialNumbers. Any
# stable constant works here; it only needs to be reused across runs so the
# same (name, version) always yields the same urn:uuid.
_OLV_UUID_NAMESPACE = uuid.UUID("b9f1c8b0-5b30-4c1a-8f2e-6a2b8e2f0c11")

# Search order for a system Boost install's boost/version.hpp, matching
# common.cmake's CMAKE_PREFIX_PATH hints (docs/PLAN.md / THIRD_PARTY.md).
_BOOST_SEARCH_DIRS = [
    ("$BOOST_ROOT", os.environ.get("BOOST_ROOT")),
    ("$BOOST_ROOT/include", (os.path.join(os.environ["BOOST_ROOT"], "include")
                              if os.environ.get("BOOST_ROOT") else None)),
    ("$HOMEBREW_PREFIX/include", (os.path.join(os.environ["HOMEBREW_PREFIX"], "include")
                                   if os.environ.get("HOMEBREW_PREFIX") else None)),
    ("/home/linuxbrew/.linuxbrew/include", "/home/linuxbrew/.linuxbrew/include"),
    ("/usr/include", "/usr/include"),
    ("/usr/local/include", "/usr/local/include"),
]


def _boost_version_string(raw: int) -> str:
    """BOOST_VERSION is MAJOR*100000 + MINOR*100 + PATCH, e.g. 109000 -> 1.90.0."""
    major = raw // 100000
    minor = (raw // 100) % 1000
    patch = raw % 100
    return f"{major}.{minor}.{patch}"


def detect_boost_version() -> tuple[str, str | None]:
    """Returns (version_string, header_path_used). version is "unknown" (and a
    warning is printed to stderr) if boost/version.hpp isn't found anywhere in
    the search path."""
    for _label, directory in _BOOST_SEARCH_DIRS:
        if not directory:
            continue
        header = os.path.join(directory, "boost", "version.hpp")
        if not os.path.isfile(header):
            continue
        try:
            with open(header, "r", encoding="utf-8", errors="replace") as fh:
                text = fh.read()
        except OSError:
            continue
        match = re.search(r"#define\s+BOOST_VERSION\s+(\d+)", text)
        if match:
            return _boost_version_string(int(match.group(1))), header
    print(
        "gen_sbom.py: warning: could not find boost/version.hpp in any of "
        "$BOOST_ROOT, $BOOST_ROOT/include, $HOMEBREW_PREFIX/include, "
        "/home/linuxbrew/.linuxbrew/include, /usr/include, /usr/local/include "
        "-- recording Boost version as 'unknown'",
        file=sys.stderr,
    )
    return "unknown", None


def _serial_number(component_name: str, version: str) -> str:
    return "urn:uuid:" + str(uuid.uuid5(_OLV_UUID_NAMESPACE, f"{component_name}@{version}"))


def _timestamp() -> str:
    # Nondeterministic by nature (current UTC time of generation); everything
    # else in the BOM is deterministic for a given environment.
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def _license(license_id: str) -> dict:
    return {"license": {"id": license_id}}


def _sha256_file(path: str) -> str:
    """Streams the file in 64 KiB chunks to compute its SHA-256 hex digest
    (avoids loading whole images into memory)."""
    digest = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(65536), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _repo_root() -> str:
    return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def frontend_asset_components() -> list[dict]:
    """Builds one CycloneDX "file" component per vendored frontend image
    asset in _FRONTEND_ASSETS, hashing each file at generation time. If an
    asset is missing on disk, it's skipped with a warning to stderr rather
    than failing the run -- mirroring detect_boost_version()'s tolerance for
    a missing dependency."""
    repo_root = _repo_root()
    components = []
    for asset in _FRONTEND_ASSETS:
        abs_path = os.path.join(repo_root, asset["path"])
        if not os.path.isfile(abs_path):
            print(
                f"gen_sbom.py: warning: frontend asset not found: {asset['path']} "
                "-- omitting from frontend BOM",
                file=sys.stderr,
            )
            continue
        components.append({
            "type": "file",
            "name": asset["path"],
            "version": asset["version"],
            "description": f"{asset['product']}. Source: {asset['source_url']}",
            "licenses": [_ASSET_LICENSE],
            "hashes": [{"alg": "SHA-256", "content": _sha256_file(abs_path)}],
            "externalReferences": [
                {"type": "distribution", "url": asset["source_url"]},
            ],
        })
    return components


def build_backend_bom() -> tuple[dict, str]:
    boost_version, boost_header = detect_boost_version()
    bom = {
        "bomFormat": "CycloneDX",
        "specVersion": "1.5",
        "serialNumber": _serial_number("olv-backend", OLV_VERSION),
        "version": 1,
        "metadata": {
            "timestamp": _timestamp(),
            "component": {
                "type": "application",
                "name": "olv-backend",
                "version": OLV_VERSION,
                "licenses": [_license("MIT")],
            },
        },
        "components": [
            {
                "type": "library",
                "name": "boost",
                "version": boost_version,
                "licenses": [_license("BSL-1.0")],
                "purl": f"pkg:generic/boost@{boost_version}",
                "description": (
                    "Header-only usage (Asio, Beast, core) by olv_backend and "
                    "olv_sim; not vendored, expected as a system/toolchain "
                    "dependency (see THIRD_PARTY.md)."
                    + (f" Detected from {boost_header}." if boost_header else "")
                ),
            }
        ],
    }
    summary = f"backend: olv-backend@{OLV_VERSION} (MIT); boost@{boost_version} (BSL-1.0)"
    return bom, summary


def build_frontend_bom() -> tuple[dict, str]:
    components = frontend_asset_components()
    bom = {
        "bomFormat": "CycloneDX",
        "specVersion": "1.5",
        "serialNumber": _serial_number("olv-frontend", OLV_VERSION),
        "version": 1,
        "metadata": {
            "timestamp": _timestamp(),
            "component": {
                "type": "application",
                "name": "olv-frontend",
                "version": OLV_VERSION,
                "licenses": [_license("MIT")],
            },
            "properties": [
                {
                    "name": "olv:notes",
                    "value": (
                        "no third-party runtime code dependencies; Node.js is a "
                        "development-only test runner (node --test frontend/tests/). "
                        "Two public-domain NASA image assets are vendored as static "
                        "files (frontend/assets/) -- see the 'file'-type components "
                        "below, not third-party code."
                    ),
                }
            ],
        },
        "components": components,
    }
    summary = (
        f"frontend: olv-frontend@{OLV_VERSION} (MIT); 0 third-party code components; "
        f"{len(components)} vendored asset component(s) (public domain)"
    )
    return bom, summary


def write_bom(bom: dict, out_dir: str, filename: str) -> str:
    os.makedirs(out_dir, exist_ok=True)
    path = os.path.join(out_dir, filename)
    with open(path, "w", encoding="utf-8") as fh:
        json.dump(bom, fh, indent=2, sort_keys=False)
        fh.write("\n")
    return path


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description="Generate CycloneDX 1.5 SBOMs for olv-backend "
                                                  "and olv-frontend.")
    parser.add_argument("--out", default="sbom", help="output directory (default: sbom/)")
    parser.add_argument("--print", dest="print_only", action="store_true",
                         help="print BOMs to stdout instead of writing files")
    args = parser.parse_args(argv)

    backend_bom, backend_summary = build_backend_bom()
    frontend_bom, frontend_summary = build_frontend_bom()

    if args.print_only:
        print("// sbom/backend.cdx.json")
        print(json.dumps(backend_bom, indent=2))
        print("// sbom/frontend.cdx.json")
        print(json.dumps(frontend_bom, indent=2))
    else:
        backend_path = write_bom(backend_bom, args.out, "backend.cdx.json")
        frontend_path = write_bom(frontend_bom, args.out, "frontend.cdx.json")
        print(f"wrote {backend_path}")
        print(f"wrote {frontend_path}")

    # License report (always printed, one line per BOM).
    print(f"license report: {backend_summary}")
    print(f"license report: {frontend_summary}")

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
