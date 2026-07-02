// sun.js — approximate solar direction in Earth-Centred Earth-Fixed (ECEF).
//
// Algorithm: low-precision solar position, Astronomical Almanac approximation
// (the "Low precision formulae for the Sun's coordinates" reduced to a unit
// ECEF direction). Accuracy is ~+/-1 degree, which is sufficient for lighting
// and a Sun billboard. Steps:
//   Unix ms -> Julian Date (JD) -> n = JD - 2451545.0 (days from J2000.0)
//   mean longitude   L = 280.460 + 0.9856474 n            (deg, wrapped)
//   mean anomaly     g = 357.528 + 0.9856003 n            (deg, wrapped)
//   ecliptic long.   lambda = L + 1.915 sin g + 0.020 sin 2g   (deg)
//   obliquity        eps = 23.439 - 0.0000004 n           (deg)
//   right ascension  RA  = atan2(cos eps sin lambda, cos lambda)
//   declination      dec = asin(sin eps sin lambda)
//   GMST(deg) = 280.46061837 + 360.98564736629 (JD - 2451545.0)   (wrapped)
//   ECEF dir  = [cos dec cos(RA - GMST), cos dec sin(RA - GMST), sin dec]
//
// IMPORT-SAFE: pure math, no DOM / globals.

const DEG = Math.PI / 180;

/** Wrap an angle in degrees to [0, 360). */
function wrapDeg(d) {
  const r = d % 360;
  return r < 0 ? r + 360 : r;
}

/**
 * Approximate unit vector from Earth's centre toward the Sun, in ECEF.
 * @param {Date|number} dateOrMs a Date or Unix time in milliseconds
 * @returns {[number, number, number]} unit vector (+Z toward the north pole)
 */
export function sunDirectionEcef(dateOrMs) {
  const ms = dateOrMs instanceof Date ? dateOrMs.getTime() : Number(dateOrMs);

  const JD = ms / 86400000 + 2440587.5; // Unix epoch (1970-01-01) = JD 2440587.5
  const n = JD - 2451545.0;             // days since J2000.0

  const L = wrapDeg(280.460 + 0.9856474 * n) * DEG;   // mean longitude
  const g = wrapDeg(357.528 + 0.9856003 * n) * DEG;   // mean anomaly
  const lambda = L + (1.915 * Math.sin(g) + 0.020 * Math.sin(2 * g)) * DEG;
  const eps = (23.439 - 0.0000004 * n) * DEG;         // obliquity of the ecliptic

  const ra = Math.atan2(Math.cos(eps) * Math.sin(lambda), Math.cos(lambda));
  const dec = Math.asin(Math.sin(eps) * Math.sin(lambda));

  const gmst = wrapDeg(280.46061837 + 360.98564736629 * n) * DEG;
  const h = ra - gmst; // hour angle of the subsolar point

  const cd = Math.cos(dec);
  let x = cd * Math.cos(h);
  let y = cd * Math.sin(h);
  let z = Math.sin(dec);

  // Normalise for numerical safety (analytically already unit length).
  const len = Math.hypot(x, y, z) || 1;
  return [x / len, y / len, z / len];
}
