# SBOM strategy

Orbital LOS Viewer ships two Software Bills of Materials, one per shippable
artifact. Both are **hand-maintained, committed files** in `sbom/` — there is
no generator script. Edit them directly whenever a dependency, version, or
vendored asset changes (see [When to update](#when-to-update)).

## What's in each BOM

- **`sbom/backend.cdx.json`** covers `olv_backend` (and, since they share the
  same dependencies, `olv_sim`). `metadata.component` describes
  `olv-backend@0.1.0` (MIT). `components` lists exactly two entries:
  - **Boost** (type `library`, license `BSL-1.0`), used header-only (Asio,
    Beast, core). Boost is not vendored; the recorded version is the one the
    shipped binaries are actually built against — the `boost-devel` package
    in the `olv-builder` image (`containers/Dockerfile.builder`, currently
    `1.83.0` on `rockylinux:10.2`), not whatever happens to be on a
    developer's host. Check it with
    `docker run --rm localhost/olv-builder:latest rpm -q boost-devel`. The
    purl is `pkg:generic/boost@<version>`.
  - **open-dis-cpp** (type `library`, license `BSD-2-Clause`), the pinned
    `OPEN_DIS_VERSION` (`"1.2.0"`, kept in sync with `containers/Dockerfile.builder`)
    used by `olv_backend --input-mode dis` and `olv_sim --protocol dis`.
    The Dockerfile pin is authoritative; the purl is
    `pkg:github/open-dis/open-dis-cpp@v1.2.0`.
- **`sbom/frontend.cdx.json`** covers the static frontend:
  `metadata.component` is `olv-frontend@0.1.0` (MIT). `components` lists
  **zero third-party code** (no frameworks, no bundler, no vendored JS) but
  **up to two `file`-type components** for the vendored NASA Earth imagery
  textures in `frontend/assets/` (`earth_day.jpg`, `earth_night.jpg`) used by
  the WebGL globe. These are data, not code, but are still tracked in the
  BOM for license/provenance visibility. Each asset component has:
  - `version` — the imagery vintage (`"2004.12"` / `"2012"`), not a software
    release, since a static image has no other natural version axis;
  - `description` — the NASA product name and source URL;
  - `licenses` — `{"license": {"name": "Public domain (NASA Media Usage
    Guidelines)", "url": "https://www.nasa.gov/nasa-brand-center/images-and-media/"}}`
    (not an SPDX id, since this isn't an SPDX-listed license);
  - `hashes` — the `SHA-256` digest of the committed file
    (`sha256sum frontend/assets/earth_*.jpg`); it must be updated whenever
    the file content changes;
  - `externalReferences` — a `distribution`-type reference to the original
    NASA source URL.

  `metadata.properties` carries a note
  clarifying that these are vendored static assets, not code, and that
  Node.js is a development-only test runner (`node --test
  frontend/tests/`), never a runtime dependency. Full provenance (retrieval
  date, dimensions, credit) lives in `frontend/assets/README.md`; the
  dependency/license table entry is in `THIRD_PARTY.md`.

Each BOM has a fixed `serialNumber` (`urn:uuid:...`) that stays the same
across edits; per CycloneDX, bump the top-level `version` integer when
revising a BOM for a new project release rather than minting a new serial.
`metadata.timestamp` is the UTC time of the last edit — update it whenever
the file changes.

## Format choice: CycloneDX 1.5 JSON

CycloneDX was chosen over SPDX because its JSON schema is compact, has
first-class `licenses`/`purl` fields per component, and is directly consumable
by common scanners (Grype, Trivy, Dependency-Track, OWASP tooling) without a
conversion step. Schema version 1.5 is current and widely supported as of
this writing.

## Checking the files

Quick sanity checks after an edit (no extra tooling needed):

```sh
# Well-formed JSON
python3 -m json.tool sbom/backend.cdx.json  >/dev/null
python3 -m json.tool sbom/frontend.cdx.json >/dev/null

# Asset hashes match the committed files
sha256sum frontend/assets/earth_day.jpg frontend/assets/earth_night.jpg
grep -A1 '"SHA-256"' sbom/frontend.cdx.json
```

## Sample output (backend BOM, abridged)

```json
{
  "bomFormat": "CycloneDX",
  "specVersion": "1.5",
  "serialNumber": "urn:uuid:52c5b330-9adf-59af-a07b-b87cb29750a6",
  "version": 1,
  "metadata": {
    "timestamp": "2026-09-26T12:00:00Z",
    "component": {
      "type": "application",
      "name": "olv-backend",
      "version": "0.1.0",
      "licenses": [{ "license": { "id": "MIT" } }]
    }
  },
  "components": [
    {
      "type": "library",
      "name": "boost",
      "version": "1.83.0",
      "licenses": [{ "license": { "id": "BSL-1.0" } }],
      "purl": "pkg:generic/boost@1.83.0"
    },
    {
      "type": "library",
      "name": "open-dis-cpp",
      "version": "1.2.0",
      "licenses": [{ "license": { "id": "BSD-2-Clause" } }],
      "purl": "pkg:github/open-dis/open-dis-cpp@v1.2.0"
    }
  ]
}
```

## Sample output (frontend BOM, one asset component, abridged)

```json
{
  "type": "file",
  "name": "frontend/assets/earth_day.jpg",
  "version": "2004.12",
  "description": "NASA Blue Marble: Next Generation. Source: https://eoimages.gsfc.nasa.gov/images/imagerecords/73000/73909/world.topo.bathy.200412.3x5400x2700.jpg",
  "licenses": [
    {
      "license": {
        "name": "Public domain (NASA Media Usage Guidelines)",
        "url": "https://www.nasa.gov/nasa-brand-center/images-and-media/"
      }
    }
  ],
  "hashes": [
    { "alg": "SHA-256", "content": "a9f0088972dee0254610af851c4d6838ca3f2cf79176987e0a5713e2c15ec042" }
  ],
  "externalReferences": [
    { "type": "distribution", "url": "https://eoimages.gsfc.nasa.gov/images/imagerecords/73000/73909/world.topo.bathy.200412.3x5400x2700.jpg" }
  ]
}
```

## Feeding the BOMs to a scanner

Any CycloneDX-aware scanner can consume these files directly, e.g.
[Grype](https://github.com/anchore/grype) (optional; not a project
dependency):

```sh
grype sbom:sbom/backend.cdx.json
grype sbom:sbom/frontend.cdx.json
```

Since Boost is header-only and not vendored into the repo, a scanner will
generally only be able to check the *declared* version against known CVEs
(it has no archive to hash) — that's expected and sufficient here: the
purpose is dependency/license visibility, not binary provenance.

## When to update

Edit `sbom/*.cdx.json` (and bump `metadata.timestamp`) whenever:

- The Boost version in the `olv-builder` image changes (base image bump in
  `containers/Dockerfile.builder`, or a new `boost-devel` build), or the
  minimum in `find_package(Boost X.Y REQUIRED CONFIG)` in the top-level
  `CMakeLists.txt` changes.
- The pinned `open-dis-cpp` version changes (`OPEN_DIS_VERSION` build arg in
  `containers/Dockerfile.builder`) — update `version` and `purl`.
- The project version changes (`project(... VERSION X.Y.Z)` in
  `CMakeLists.txt`) — update `metadata.component.version` in both BOMs.
- Any new runtime dependency is introduced anywhere in the repo (backend,
  simulator, or frontend) — add a component and a `THIRD_PARTY.md` row
  together.
- A vendored frontend asset changes on disk, is added, or is removed — update
  its component (including the `SHA-256` hash), `frontend/assets/README.md`,
  and `THIRD_PARTY.md` together.
- Before a release/tag, so the committed SBOMs match what's shipped.

`sbom/` is committed (it is excluded from container build contexts via
`.dockerignore`, since nothing in the images needs it).

## Generating with a tool (optional)

[`syft`](https://github.com/anchore/syft) can generate CycloneDX SBOMs by
scanning the filesystem/container image directly and is a reasonable
alternative where available (e.g. `syft dir:. -o cyclonedx-json`), including
against the built container images from `containers/Dockerfile.backend`,
`containers/Dockerfile.simulator` (and `containers/Dockerfile.builder`) and
`containers/Containerfile.frontend`.
Its output is a useful cross-check, but the
hand-maintained files in `sbom/` remain canonical: they encode
project-specific knowledge a scanner can't infer (e.g. "Boost is header-only,
not vendored", asset provenance).
