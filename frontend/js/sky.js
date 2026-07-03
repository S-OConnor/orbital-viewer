// sky.js — low-precision ephemerides for the celestial background: sidereal
// time, the Moon, the five naked-eye planets, and the built-in star catalog,
// all reduced to unit direction vectors in Earth-Centred Earth-Fixed (ECEF).
//
// This module is the mathematical companion to sun.js. It shares sun.js's
// exact time base (Unix ms -> Julian Date -> days from J2000.0), its exact
// GMST formula, and its exact ECEF convention, so every body in the sky is
// mutually consistent with the Sun. Precision is the same "visual, not
// almanac" class as sun.js (~arcminute to ~0.5 degree).
//
// Common pipeline (identical in spirit to sun.js):
//   Unix ms -> JD = ms/86400000 + 2440587.5 -> n = JD - 2451545.0 (days from
//   J2000.0) -> T = n/36525 (Julian centuries). Work in degrees, convert with
//   DEG, wrap with wrapDeg. GMST(deg) = 280.46061837 + 360.98564736629 n
//   (wrapped). An inertial (ECI) unit vector [cos d cos RA, cos d sin RA,
//   sin d] rotates to ECEF by Rz(-GMST), i.e. ECEF longitude = RA - GMST.
//   Obliquity eps = 23.439 - 0.0000004 n (deg), same as sun.js.
//
// Algorithms & provenance (all first-party implementations of public formulae):
//   * Moon — Astronomical Almanac "low precision" lunar theory: ecliptic
//     longitude/latitude as a short Fourier series in the fundamental lunar
//     arguments, then (lambda, beta) -> equatorial (RA, dec) via the obliquity.
//     Accuracy ~arcminute-to-degree class. Phase (illuminated fraction) from
//     the Sun-Earth-Moon elongation using sun.js's Sun direction.
//   * Planets — JPL "Keplerian Elements for Approximate Positions of the Major
//     Planets" (Standish), valid 1800-2050: elements = element0 + rate*T,
//     solve Kepler's equation, heliocentric ecliptic -> geocentric (subtract
//     Earth/EM-Barycentre) -> equatorial (obliquity) -> ECEF. Magnitudes from
//     the rough distance law V0 + 5 log10(r * Delta) for on-screen sizing only.
//   * Stars — precomputed J2000 ECI unit vectors from STAR_CATALOG, rotated to
//     ECEF each call by a single shared GMST rotation (no per-call allocation).
//
// IMPORT-SAFE: pure math, no DOM / WebGL / globals. The only side effect at
// import is filling the two precomputed star typed arrays below.

import { STAR_CATALOG } from './star_catalog.js';
import { sunDirectionEcef } from './sun.js';

const DEG = Math.PI / 180;

/** Wrap an angle in degrees to [0, 360). */
function wrapDeg(d) {
  const r = d % 360;
  return r < 0 ? r + 360 : r;
}

/** Wrap an angle in degrees to (-180, 180] (used for the mean anomaly). */
function wrapDeg180(d) {
  const r = wrapDeg(d);
  return r > 180 ? r - 360 : r;
}

/** Unix milliseconds from a Date or a numeric ms input (matches sun.js). */
function toMs(dateOrMs) {
  return dateOrMs instanceof Date ? dateOrMs.getTime() : Number(dateOrMs);
}

/** Sine of an angle given in degrees. */
function sinDeg(d) {
  return Math.sin(d * DEG);
}

/** Cosine of an angle given in degrees. */
function cosDeg(d) {
  return Math.cos(d * DEG);
}

/** Days since J2000.0 (n) for a Date or Unix ms — the shared time argument. */
function daysSinceJ2000(ms) {
  const JD = ms / 86400000 + 2440587.5; // Unix epoch = JD 2440587.5
  return JD - 2451545.0;                // days since J2000.0 (2000-01-01 12:00 UTC)
}

/**
 * Greenwich Mean Sidereal Time as an angle in radians, [0, 2*pi).
 * Identical formula and epoch to sun.js so the whole sky shares one sidereal
 * time with the Sun.
 * @param {Date|number} dateOrMs a Date or Unix time in milliseconds
 * @returns {number} GMST in radians, in [0, 2*pi)
 */
export function gmstRad(dateOrMs) {
  const n = daysSinceJ2000(toMs(dateOrMs));
  return wrapDeg(280.46061837 + 360.98564736629 * n) * DEG;
}

/**
 * J2000 equatorial coordinates (right ascension / declination, RADIANS) to an
 * inertial (ECI) unit vector: [cos d cos RA, cos d sin RA, sin d].
 * @param {number} raRad  right ascension in radians
 * @param {number} decRad declination in radians
 * @returns {[number, number, number]} unit vector in the ECI frame
 */
export function raDecToEci(raRad, decRad) {
  const cd = Math.cos(decRad);
  return [cd * Math.cos(raRad), cd * Math.sin(raRad), Math.sin(decRad)];
}

/**
 * Rotate an ECI unit vector into ECEF by Rz(-gmst). This yields ECEF longitude
 * = RA - GMST, matching sun.js's [cos d cos(RA-GMST), cos d sin(RA-GMST), sin d].
 * @param {[number, number, number]} vEci inertial (ECI) vector
 * @param {number} gmst Greenwich Mean Sidereal Time in radians
 * @returns {[number, number, number]} the same vector expressed in ECEF
 */
export function eciToEcef(vEci, gmst) {
  const cg = Math.cos(gmst);
  const sg = Math.sin(gmst);
  return [
    vEci[0] * cg + vEci[1] * sg,
    -vEci[0] * sg + vEci[1] * cg,
    vEci[2],
  ];
}

/**
 * Approximate unit vector from Earth's centre toward the Moon, in ECEF.
 * Astronomical Almanac low-precision lunar series (ecliptic lon/lat) reduced to
 * equatorial coordinates via the obliquity, then rotated into ECEF by GMST.
 * Accuracy ~arcminute-to-degree class — enough for a Moon billboard.
 * @param {Date|number} dateOrMs a Date or Unix time in milliseconds
 * @returns {[number, number, number]} unit vector (+Z toward the north pole)
 */
export function moonDirectionEcef(dateOrMs) {
  const ms = toMs(dateOrMs);
  const n = daysSinceJ2000(ms);
  const T = n / 36525; // Julian centuries from J2000.0

  // Ecliptic longitude (deg): mean longitude plus the leading periodic terms.
  const lambda =
    218.32 + 481267.881 * T +
    6.29 * sinDeg(135.0 + 477198.87 * T) -
    1.27 * sinDeg(259.3 - 413335.36 * T) +
    0.66 * sinDeg(235.7 + 890534.22 * T) +
    0.21 * sinDeg(269.9 + 954397.74 * T) -
    0.19 * sinDeg(357.5 + 35999.05 * T) -
    0.11 * sinDeg(186.5 + 966404.03 * T);

  // Ecliptic latitude (deg): leading periodic terms about the ecliptic plane.
  const beta =
    5.13 * sinDeg(93.3 + 483202.02 * T) +
    0.28 * sinDeg(228.2 + 960400.89 * T) -
    0.28 * sinDeg(318.3 + 6003.15 * T) -
    0.17 * sinDeg(217.6 - 407332.20 * T);

  const eps = (23.439 - 0.0000004 * n) * DEG; // obliquity of the ecliptic

  // Ecliptic (lambda, beta) -> equatorial (RA, dec) via a rotation by eps.
  const sinB = sinDeg(beta);
  const cosB = cosDeg(beta);
  const sinL = sinDeg(lambda);
  const cosL = cosDeg(lambda);
  const sinDec = sinB * Math.cos(eps) + cosB * Math.sin(eps) * sinL;
  const dec = Math.asin(sinDec);
  const ra = Math.atan2(
    cosB * sinL * Math.cos(eps) - sinB * Math.sin(eps),
    cosB * cosL,
  );

  // ECEF direction: hour angle h = RA - GMST (same convention as sun.js).
  const h = ra - gmstRad(ms);
  const cd = Math.cos(dec);
  const x = cd * Math.cos(h);
  const y = cd * Math.sin(h);
  const z = sinDec;

  // Normalise for numerical safety (analytically already unit length).
  const len = Math.hypot(x, y, z) || 1;
  return [x / len, y / len, z / len];
}

/**
 * Illuminated fraction of the Moon's disc, in [0, 1] (0 = new, 1 = full).
 * Uses the Sun-Earth-Moon elongation psi from the unit ECEF Sun and Moon
 * directions: fraction = (1 - cos psi) / 2. This is the cheap, robust form —
 * exact at the new/full extremes and monotonic between them.
 * @param {Date|number} dateOrMs a Date or Unix time in milliseconds
 * @returns {number} illuminated fraction in [0, 1]
 */
export function moonIlluminatedFraction(dateOrMs) {
  const s = sunDirectionEcef(dateOrMs);
  const m = moonDirectionEcef(dateOrMs);
  const cosPsi = s[0] * m[0] + s[1] * m[1] + s[2] * m[2];
  const f = (1 - cosPsi) / 2;
  return f < 0 ? 0 : f > 1 ? 1 : f; // clamp against rounding at the extremes
}

/** Naked-eye planet names, fixed order. */
export const PLANET_NAMES = ['mercury', 'venus', 'mars', 'jupiter', 'saturn'];

// JPL "Keplerian Elements for Approximate Positions of the Major Planets"
// (Standish), valid 1800-2050. Each element is [J2000 value, per-century rate];
// a is in au, all angles are in degrees. Earth is represented by the
// Earth-Moon barycentre ("embary"), used to convert heliocentric -> geocentric.
//   a  semi-major axis        e  eccentricity          I  inclination
//   L  mean longitude         wbar longitude of perihelion   node  longitude of ascending node
const PLANET_ELEMENTS = {
  mercury: {
    a: [0.38709927, 0.00000037], e: [0.20563593, 0.00001906], I: [7.00497902, -0.00594749],
    L: [252.25032350, 149472.67411175], wbar: [77.45779628, 0.16047689], node: [48.33076593, -0.12534081],
  },
  venus: {
    a: [0.72333566, 0.00000390], e: [0.00677672, -0.00004107], I: [3.39467605, -0.00078890],
    L: [181.97909950, 58517.81538729], wbar: [131.60246718, 0.00268329], node: [76.67984255, -0.27769418],
  },
  embary: {
    a: [1.00000261, 0.00000562], e: [0.01671123, -0.00004392], I: [-0.00001531, -0.01294668],
    L: [100.46457166, 35999.37244981], wbar: [102.93768193, 0.32327364], node: [0.0, 0.0],
  },
  mars: {
    a: [1.52371034, 0.00001847], e: [0.09339410, 0.00007882], I: [1.84969142, -0.00813131],
    L: [-4.55343205, 19140.30268499], wbar: [-23.94362959, 0.44441088], node: [49.55953891, -0.29257343],
  },
  jupiter: {
    a: [5.20288700, -0.00011607], e: [0.04838624, -0.00013253], I: [1.30439695, -0.00183714],
    L: [34.39644051, 3034.74612775], wbar: [14.72847983, 0.21252668], node: [100.47390909, 0.20469106],
  },
  saturn: {
    a: [9.53667594, -0.00125060], e: [0.05386179, -0.00050991], I: [2.48599187, 0.00193609],
    L: [49.95424423, 1222.49362201], wbar: [92.59887831, -0.41897216], node: [113.66242448, -0.28867794],
  },
};

/** Rough base (mean-opposition-ish) visual magnitudes for the sizing formula. */
const PLANET_V0 = {
  mercury: -0.42, venus: -4.40, mars: -1.52, jupiter: -9.40, saturn: -8.88,
};

/**
 * Heliocentric J2000 ecliptic rectangular coordinates (au) of a body from its
 * Keplerian elements at Julian-century time T. Solves Kepler's equation by
 * Newton iteration, then rotates the orbital-plane position by (omega, I, Omega).
 * @param {object} el element table entry from PLANET_ELEMENTS
 * @param {number} T Julian centuries since J2000.0
 * @returns {[number, number, number]} [x, y, z] in au, ecliptic frame
 */
function heliocentricEcliptic(el, T) {
  const a = el.a[0] + el.a[1] * T;                    // semi-major axis (au)
  const e = el.e[0] + el.e[1] * T;                    // eccentricity
  const I = (el.I[0] + el.I[1] * T) * DEG;            // inclination (rad)
  const L = el.L[0] + el.L[1] * T;                    // mean longitude (deg)
  const wbar = el.wbar[0] + el.wbar[1] * T;           // longitude of perihelion (deg)
  const node = el.node[0] + el.node[1] * T;           // longitude of ascending node (deg)

  const omega = (wbar - node) * DEG;                  // argument of perihelion (rad)
  const Omega = node * DEG;                           // ascending node (rad)
  const M = wrapDeg180(L - wbar) * DEG;               // mean anomaly (rad), in (-pi, pi]

  // Solve Kepler's equation M = E - e sin E for the eccentric anomaly E (rad).
  let E = M + e * Math.sin(M);
  for (let k = 0; k < 6; k++) {
    E -= (E - e * Math.sin(E) - M) / (1 - e * Math.cos(E));
  }

  // Position in the orbital plane (perifocal), au.
  const xp = a * (Math.cos(E) - e);
  const yp = a * Math.sqrt(1 - e * e) * Math.sin(E);

  // Rotate perifocal -> J2000 ecliptic by omega (in plane), I (tilt), Omega (node).
  const cw = Math.cos(omega), sw = Math.sin(omega);
  const cO = Math.cos(Omega), sO = Math.sin(Omega);
  const cI = Math.cos(I), sI = Math.sin(I);
  const x = (cw * cO - sw * sO * cI) * xp + (-sw * cO - cw * sO * cI) * yp;
  const y = (cw * sO + sw * cO * cI) * xp + (-sw * sO + cw * cO * cI) * yp;
  const z = (sw * sI) * xp + (cw * sI) * yp;
  return [x, y, z];
}

/**
 * Geocentric apparent positions of the five naked-eye planets in ECEF.
 * Heliocentric ecliptic positions (planet and Earth) -> geocentric ecliptic ->
 * equatorial (obliquity) -> ECEF (GMST). Magnitude is the rough distance law
 * V0 + 5 log10(r * Delta), used only for on-screen sizing.
 * @param {Date|number} dateOrMs a Date or Unix time in milliseconds
 * @returns {Array<{name:string, dir:[number,number,number], mag:number}>}
 *   in PLANET_NAMES order; dir is a unit ECEF vector; mag is finite
 */
export function planetsEcef(dateOrMs) {
  const ms = toMs(dateOrMs);
  const n = daysSinceJ2000(ms);
  const T = n / 36525;
  const eps = (23.439 - 0.0000004 * n) * DEG; // obliquity of the ecliptic
  const cosEps = Math.cos(eps);
  const sinEps = Math.sin(eps);
  const g = gmstRad(ms);

  const earth = heliocentricEcliptic(PLANET_ELEMENTS.embary, T);

  return PLANET_NAMES.map((name) => {
    const helio = heliocentricEcliptic(PLANET_ELEMENTS[name], T);

    // Geocentric ecliptic vector = planet - Earth (au).
    const gx = helio[0] - earth[0];
    const gy = helio[1] - earth[1];
    const gz = helio[2] - earth[2];

    // Ecliptic -> equatorial (J2000/ECI) by a rotation of eps about the X axis.
    const xeq = gx;
    const yeq = gy * cosEps - gz * sinEps;
    const zeq = gy * sinEps + gz * cosEps;

    // Unit direction in ECI, then rotate into ECEF by GMST.
    const len = Math.hypot(xeq, yeq, zeq) || 1;
    const dir = eciToEcef([xeq / len, yeq / len, zeq / len], g);

    // Rough apparent magnitude for sizing: r = heliocentric, Delta = geocentric.
    const r = Math.hypot(helio[0], helio[1], helio[2]);
    const delta = Math.hypot(gx, gy, gz);
    const mag = PLANET_V0[name] + 5 * Math.log10(r * delta);

    return { name, dir, mag };
  });
}

/** Number of stars in the built-in catalog (=== STAR_CATALOG.length). */
export const STAR_COUNT = STAR_CATALOG.length;

/** Per-star apparent visual magnitude, index-aligned with the catalog. */
export const STAR_MAGS = new Float32Array(STAR_COUNT);

// Precomputed J2000 ECI unit vectors (3 components per star), filled once at
// import so starsEcefInto only has to apply the GMST rotation each call.
const STAR_ECI = new Float32Array(STAR_COUNT * 3);
for (let i = 0; i < STAR_COUNT; i++) {
  const s = STAR_CATALOG[i];
  STAR_MAGS[i] = s.mag;
  const v = raDecToEci(s.ra * DEG, s.dec * DEG);
  STAR_ECI[i * 3] = v[0];
  STAR_ECI[i * 3 + 1] = v[1];
  STAR_ECI[i * 3 + 2] = v[2];
}

/**
 * Rotate every catalog star into an ECEF unit vector for time `dateOrMs`,
 * writing 3 components per star into `out` (must be length >= STAR_COUNT*3).
 * Computes GMST once, then applies the Rz(-GMST) rotation (inlined eciToEcef
 * math with a shared cos/sin) to each precomputed J2000 unit vector.
 * Does NOT allocate on the heap per call.
 * @param {Date|number} dateOrMs a Date or Unix time in milliseconds
 * @param {Float32Array|number[]} out destination, length >= STAR_COUNT*3
 * @returns {number} STAR_COUNT (the number of 3-vectors written)
 */
export function starsEcefInto(dateOrMs, out) {
  const g = gmstRad(dateOrMs);
  const cg = Math.cos(g);
  const sg = Math.sin(g);
  for (let i = 0; i < STAR_COUNT; i++) {
    const b = i * 3;
    const x = STAR_ECI[b];
    const y = STAR_ECI[b + 1];
    const z = STAR_ECI[b + 2];
    out[b] = x * cg + y * sg;      // ECEF x = ECI (Rz(-g)) row 0
    out[b + 1] = -x * sg + y * cg; // ECEF y
    out[b + 2] = z;                // ECEF z (rotation about Z leaves z fixed)
  }
  return STAR_COUNT;
}
