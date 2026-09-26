# Frontend static assets — provenance

This directory holds vendored, first-party-served static image assets used by
the frontend's WebGL Earth globe. Both files are public-domain NASA Earth
imagery — no license grant is required to use, modify, or redistribute them,
but attribution is recorded here as good practice.

| File | Product | Source | Retrieved | Dimensions | SHA-256 | Size | Credit | License |
|---|---|---|---|---|---|---|---|---|
| `earth_day.jpg` | NASA Blue Marble: Next Generation (Dec 2004) | [eoimages.gsfc.nasa.gov/.../world.topo.bathy.200412.3x5400x2700.jpg](https://eoimages.gsfc.nasa.gov/images/imagerecords/73000/73909/world.topo.bathy.200412.3x5400x2700.jpg) | 2026-07-02 | 5400×2700 (equirectangular) | `a9f0088972dee0254610af851c4d6838ca3f2cf79176987e0a5713e2c15ec042` | 2,566,770 bytes | NASA Earth Observatory / Reto Stöckli | Public domain (NASA Media Usage Guidelines) |
| `earth_night.jpg` | NASA Black Marble / Suomi NPP VIIRS — "Earth at Night 2012" | [eoimages.gsfc.nasa.gov/.../dnb_land_ocean_ice.2012.3600x1800.jpg](https://eoimages.gsfc.nasa.gov/images/imagerecords/79000/79765/dnb_land_ocean_ice.2012.3600x1800.jpg) | 2026-07-02 | 3600×1800 (equirectangular) | `373e5a08c9f378a2ce6320214a613148e4b1e3946b3f39a516c9093b76cb7124` | 794,479 bytes | NASA Earth Observatory / NOAA NGDC | Public domain (NASA Media Usage Guidelines) |

## License

Both images are US-government-produced NASA imagery, generally not subject to
copyright, and free for non-commercial and commercial use per the
[NASA Media Usage Guidelines](https://www.nasa.gov/nasa-brand-center/images-and-media/).
Cite as: "Public domain (NASA Media Usage Guidelines)".

These two files are also tracked as components in the frontend CycloneDX SBOM
(`sbom/frontend.cdx.json`, hand-maintained) — see
[`docs/SBOM.md`](../../docs/SBOM.md) and [`THIRD_PARTY.md`](../../THIRD_PARTY.md).

## Processing

Both files are used exactly as retrieved — no repo-side cropping, recompression,
or resizing is performed. The frontend downsizes them to power-of-two texture
dimensions in the browser at load time (a WebGL requirement for mipmapping);
this happens client-side, on demand, and does not modify the files in this
directory.

## Air-gap posture

Both files are vendored in-repo and served same-origin as ordinary static
files by whatever HTTP server hosts the frontend (`scripts/serve_frontend.sh`,
`containers/Containerfile.frontend`, or any static file server). Nothing in
this directory is fetched from, or causes a fetch from, the network at
runtime — the air-gap posture described in the top-level README and
`THIRD_PARTY.md` is unchanged.
