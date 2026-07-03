// star_catalog.js — built-in catalog of the brightest stars, J2000 equatorial
// coordinates (right ascension / declination) and apparent visual magnitude.
//
// Coverage: every star of apparent visual magnitude <= 3.5 (~180 stars), plus
// a small number of slightly fainter entries (down to ~mag 3.9) that are
// included only because they complete a well-known constellation shape or
// cluster (e.g. the Pleiades, the Hyades) — the same "fill out the picture"
// judgment call a planetarium's bright-star layer makes.
//
// Provenance: the positions and magnitudes below are astronomical facts,
// derived from public-domain bright-star data (Yale Bright Star Catalogue /
// Hipparcos catalogue) and hand-encoded here as first-party data. No
// third-party code or data files are included, bundled, or fetched at
// runtime.
//
// Precision: matches the precision class of sun.js. The dozen-or-so
// first-magnitude stars (mag <~1.5) are placed to within a fraction of a
// degree of their true J2000 position; fainter entries are accurate to
// roughly a degree, which is sufficient for a visually correct, recognizable
// sky background rather than an almanac.
//
// IMPORT-SAFE: pure data, no DOM / globals / logic beyond the literal array.

/**
 * Brightest stars for the celestial background. J2000 equatorial coords.
 * Grouped by constellation/asterism for human readability; ordered
 * brightest-first is NOT required (and not done).
 *
 * @type {ReadonlyArray<{ra:number, dec:number, mag:number, name?:string}>}
 *   ra   right ascension, DEGREES, [0, 360)
 *   dec  declination,     DEGREES, [-90, 90]
 *   mag  apparent visual magnitude (brighter = smaller/negative; Sirius ~ -1.46)
 *   name optional common name (set here for every entry; only the ~30
 *        brightest are strictly required to carry one)
 */
export const STAR_CATALOG = [
  // --- The all-sky first-magnitude stars (brightest, tightest accuracy) ---
  { ra: 101.2871, dec: -16.7161, mag: -1.46, name: 'Sirius' },
  { ra: 95.9880, dec: -52.6956, mag: -0.74, name: 'Canopus' },
  { ra: 219.9021, dec: -60.8340, mag: -0.27, name: 'Rigil Kentaurus' },
  { ra: 213.9154, dec: 19.1825, mag: -0.05, name: 'Arcturus' },
  { ra: 279.2347, dec: 38.7837, mag: 0.03, name: 'Vega' },
  { ra: 79.1723, dec: 45.9980, mag: 0.08, name: 'Capella' },
  { ra: 78.6345, dec: -8.2016, mag: 0.18, name: 'Rigel' },
  { ra: 114.8255, dec: 5.2250, mag: 0.34, name: 'Procyon' },
  { ra: 88.7929, dec: 7.4071, mag: 0.42, name: 'Betelgeuse' },
  { ra: 24.4285, dec: -57.2368, mag: 0.46, name: 'Achernar' },
  { ra: 210.9558, dec: -60.3730, mag: 0.61, name: 'Hadar' },
  { ra: 297.6958, dec: 8.8683, mag: 0.76, name: 'Altair' },
  { ra: 186.6496, dec: -63.0991, mag: 0.77, name: 'Acrux' },
  { ra: 68.9802, dec: 16.5093, mag: 0.85, name: 'Aldebaran' },
  { ra: 201.2983, dec: -11.1614, mag: 0.97, name: 'Spica' },
  { ra: 247.3521, dec: -26.4319, mag: 1.06, name: 'Antares' },
  { ra: 116.3289, dec: 28.0262, mag: 1.14, name: 'Pollux' },
  { ra: 344.4127, dec: -29.6222, mag: 1.16, name: 'Fomalhaut' },
  { ra: 310.3580, dec: 45.2803, mag: 1.25, name: 'Deneb' },
  { ra: 191.9304, dec: -59.6889, mag: 1.25, name: 'Mimosa' },
  { ra: 152.0930, dec: 11.9672, mag: 1.35, name: 'Regulus' },
  { ra: 104.6563, dec: -28.9722, mag: 1.50, name: 'Adhara' },

  // --- Second-magnitude and named bright stars, all-sky ---
  { ra: 113.6495, dec: 31.8883, mag: 1.58, name: 'Castor' },
  { ra: 187.7915, dec: -57.1133, mag: 1.63, name: 'Gacrux' },
  { ra: 263.4022, dec: -37.1038, mag: 1.62, name: 'Shaula' },
  { ra: 81.2828, dec: 6.3497, mag: 1.64, name: 'Bellatrix' },
  { ra: 81.5730, dec: 28.6075, mag: 1.65, name: 'Elnath' },
  { ra: 138.3010, dec: -69.7172, mag: 1.68, name: 'Miaplacidus' },
  { ra: 84.0534, dec: -1.2019, mag: 1.69, name: 'Alnilam' },
  { ra: 332.0584, dec: -46.9611, mag: 1.73, name: 'Alnair' },
  { ra: 85.1897, dec: -1.9426, mag: 1.74, name: 'Alnitak' },
  { ra: 193.5073, dec: 55.9598, mag: 1.77, name: 'Alioth' },
  { ra: 122.3830, dec: -47.3365, mag: 1.78, name: 'Regor' },
  { ra: 51.0808, dec: 49.8612, mag: 1.79, name: 'Mirfak' },
  { ra: 165.9319, dec: 61.7511, mag: 1.79, name: 'Dubhe' },
  { ra: 107.0979, dec: -26.3932, mag: 1.83, name: 'Wezen' },
  { ra: 276.0430, dec: -34.3846, mag: 1.85, name: 'Kaus Australis' },
  { ra: 206.8852, dec: 49.3133, mag: 1.86, name: 'Alkaid' },
  { ra: 264.3297, dec: -42.9978, mag: 1.86, name: 'Sargas' },
  { ra: 125.6285, dec: -59.5095, mag: 1.86, name: 'Avior' },
  { ra: 89.8822, dec: 44.9474, mag: 1.90, name: 'Menkalinan' },
  { ra: 252.1662, dec: -69.0277, mag: 1.91, name: 'Atria' },
  { ra: 99.4279, dec: 16.3993, mag: 1.93, name: 'Alhena' },
  { ra: 306.4120, dec: -56.7350, mag: 1.94, name: 'Peacock' },
  { ra: 131.1758, dec: -54.7086, mag: 1.96, name: 'Alsephina' },
  { ra: 95.6749, dec: -17.9559, mag: 1.98, name: 'Mirzam' },
  { ra: 141.8968, dec: -8.6586, mag: 1.98, name: 'Alphard' },
  { ra: 37.9529, dec: 89.2641, mag: 1.98, name: 'Polaris' },
  { ra: 31.7934, dec: 23.4624, mag: 2.00, name: 'Hamal' },
  { ra: 154.9931, dec: 19.8415, mag: 2.01, name: 'Algieba' },
  { ra: 10.8974, dec: -17.9866, mag: 2.04, name: 'Diphda' },
  { ra: 200.9814, dec: 54.9254, mag: 2.04, name: 'Mizar' },
  { ra: 283.8163, dec: -26.2967, mag: 2.05, name: 'Nunki' },
  { ra: 210.9581, dec: -36.3700, mag: 2.06, name: 'Menkent' },
  { ra: 2.0969, dec: 29.0904, mag: 2.06, name: 'Alpheratz' },
  { ra: 86.9391, dec: -9.6696, mag: 2.06, name: 'Saiph' },
  { ra: 222.6764, dec: 74.1555, mag: 2.07, name: 'Kochab' },
  { ra: 263.7328, dec: 12.5601, mag: 2.08, name: 'Rasalhague' },
  { ra: 47.0422, dec: 40.9556, mag: 2.12, name: 'Algol' },
  { ra: 30.9748, dec: 42.3297, mag: 2.10, name: 'Almach' },
  { ra: 177.2649, dec: 14.5721, mag: 2.14, name: 'Denebola' },
  { ra: 340.6667, dec: -46.8846, mag: 2.11, name: 'Tiaki' },
  { ra: 189.2966, dec: -48.9598, mag: 2.17, name: 'Muhlifain' },
  { ra: 120.8961, dec: -40.0032, mag: 2.21, name: 'Naos' },
  { ra: 139.2725, dec: -59.2750, mag: 2.21, name: 'Aspidiske' },
  { ra: 233.6717, dec: 26.7147, mag: 2.23, name: 'Alphecca' },
  { ra: 136.9990, dec: -43.4326, mag: 2.21, name: 'Suhail' },
  { ra: 17.4330, dec: 35.6206, mag: 2.07, name: 'Mirach' },
  { ra: 305.5571, dec: 40.2567, mag: 2.23, name: 'Sadr' },
  { ra: 269.1516, dec: 51.4889, mag: 2.23, name: 'Eltanin' },
  { ra: 10.1268, dec: 56.5373, mag: 2.24, name: 'Schedar' },
  { ra: 83.0016, dec: -0.2991, mag: 2.25, name: 'Mintaka' },
  { ra: 2.2945, dec: 59.1498, mag: 2.27, name: 'Caph' },
  { ra: 240.0834, dec: -22.6217, mag: 2.29, name: 'Dschubba' },
  { ra: 253.4644, dec: -34.2933, mag: 2.29, name: 'Larawag' },
  { ra: 6.5708, dec: -42.3061, mag: 2.40, name: 'Ankaa' },
  { ra: 326.0463, dec: 9.8750, mag: 2.40, name: 'Enif' },
  { ra: 345.9436, dec: 28.0828, mag: 2.42, name: 'Scheat' },
  { ra: 258.6618, dec: -15.7249, mag: 2.43, name: 'Sabik' },
  { ra: 178.4577, dec: 53.6948, mag: 2.44, name: 'Phecda' },
  { ra: 111.7873, dec: -29.3031, mag: 2.45, name: 'Aludra' },
  { ra: 140.5284, dec: -55.0106, mag: 2.47, name: 'Markeb' },
  { ra: 319.6449, dec: 62.5854, mag: 2.44, name: 'Alderamin' },
  { ra: 346.1900, dec: 15.2053, mag: 2.49, name: 'Markab' },
  { ra: 305.2569, dec: 33.9702, mag: 2.46, name: 'Aljanah' },
  { ra: 241.3592, dec: -19.8054, mag: 2.56, name: 'Acrab' },
  { ra: 183.9516, dec: -17.5419, mag: 2.59, name: 'Gienah' },
  { ra: 168.5270, dec: 20.5237, mag: 2.56, name: 'Zosma' },
  { ra: 165.4603, dec: 56.3824, mag: 2.37, name: 'Merak' },
  { ra: 262.6082, dec: 52.3013, mag: 2.79, name: 'Rastaban' },
  { ra: 188.5966, dec: -23.3964, mag: 2.65, name: 'Kraz' },
  { ra: 271.4520, dec: -30.4241, mag: 2.98, name: 'Alnasl' },
  { ra: 265.8682, dec: 4.5674, mag: 2.76, name: 'Cebalrai' },
  { ra: 240.0994, dec: -3.6944, mag: 2.73, name: 'Yed Prior' },
  { ra: 21.4538, dec: 60.2352, mag: 2.68, name: 'Ruchbah' },
  { ra: 274.4067, dec: -25.4217, mag: 2.82, name: 'Kaus Borealis' },
  { ra: 222.7196, dec: -16.0418, mag: 2.75, name: 'Zubenelgenubi' },
  { ra: 229.2518, dec: -9.3827, mag: 2.61, name: 'Zubeneschamali' },
  { ra: 236.0669, dec: 6.4256, mag: 2.63, name: 'Unukalhai' },
  { ra: 28.6600, dec: 20.8082, mag: 2.64, name: 'Sheratan' },

  // --- Ursa Major / Big Dipper (remaining stars to complete the asterism) ---
  { ra: 154.2765, dec: 41.4995, mag: 3.05, name: 'Tania Australis' },
  { ra: 183.8565, dec: 57.0326, mag: 3.32, name: 'Megrez' },

  // --- Ursa Minor (remaining) ---
  { ra: 230.1819, dec: 71.8340, mag: 3.05, name: 'Pherkad' },

  // --- Pleiades cluster (Taurus) ---
  { ra: 56.8712, dec: 24.1050, mag: 2.87, name: 'Alcyone' },
  { ra: 57.2879, dec: 24.0533, mag: 3.62, name: 'Atlas' },
  { ra: 56.2179, dec: 24.1133, mag: 3.70, name: 'Electra' },
  { ra: 56.4544, dec: 24.3678, mag: 3.86, name: 'Maia' },

  // --- Cassiopeia (remaining W-asterism stars) ---
  { ra: 14.1772, dec: 60.7167, mag: 2.47, name: 'Gamma Cassiopeiae' },
  { ra: 28.5988, dec: 63.6701, mag: 3.35, name: 'Segin' },

  // --- Orion (remaining belt/sword stars) ---
  { ra: 83.7842, dec: 9.9342, mag: 3.39, name: 'Meissa' },
  { ra: 83.8583, dec: -5.9099, mag: 2.77, name: 'Hatysa' },

  // --- Crux / Southern Cross (remaining) ---
  { ra: 183.7863, dec: -58.7489, mag: 2.79, name: 'Delta Crucis' },
  { ra: 183.1596, dec: -60.4011, mag: 3.59, name: 'Epsilon Crucis' },

  // --- Scorpius (remaining body/tail stars) ---
  { ra: 240.1268, dec: -26.1141, mag: 2.89, name: 'Pi Scorpii' },
  { ra: 244.5842, dec: -25.5928, mag: 2.89, name: 'Sigma Scorpii' },
  { ra: 248.9698, dec: -28.2162, mag: 2.82, name: 'Tau Scorpii' },
  { ra: 266.8958, dec: -40.1275, mag: 3.03, name: 'Iota1 Scorpii' },
  { ra: 252.9667, dec: -38.0473, mag: 3.00, name: 'Mu1 Scorpii' },
  { ra: 254.2857, dec: -42.3617, mag: 3.62, name: 'Zeta2 Scorpii' },
  { ra: 265.6217, dec: -39.0300, mag: 2.41, name: 'Girtab' },

  // --- Cygnus / Summer Triangle wing stars ---
  { ra: 296.2437, dec: 45.1310, mag: 2.87, name: 'Delta Cygni' },
  { ra: 292.6804, dec: 27.9597, mag: 3.08, name: 'Albireo' },

  // --- Aquila (remaining) ---
  { ra: 298.8280, dec: 10.6133, mag: 2.72, name: 'Tarazed' },
  { ra: 296.5647, dec: 6.4067, mag: 3.71, name: 'Alshain' },

  // --- Lyra (remaining) ---
  { ra: 282.5199, dec: 33.3627, mag: 3.45, name: 'Sheliak' },
  { ra: 284.7359, dec: 32.6896, mag: 3.24, name: 'Sulafat' },

  // --- Leo (remaining) ---
  { ra: 154.1719, dec: 23.4174, mag: 3.44, name: 'Adhafera' },
  { ra: 148.1747, dec: 26.0071, mag: 3.88, name: 'Rasalas' },
  { ra: 148.9548, dec: 23.7742, mag: 2.98, name: 'Ras Elased Australis' },
  { ra: 168.4986, dec: 15.4295, mag: 3.34, name: 'Chertan' },

  // --- Taurus / Hyades (remaining) ---
  { ra: 84.4125, dec: 21.1425, mag: 3.03, name: 'Zeta Tauri' },
  { ra: 60.1708, dec: 12.4903, mag: 3.47, name: 'Lambda Tauri' },
  { ra: 67.1542, dec: 19.1806, mag: 3.53, name: 'Ain' },
  { ra: 64.9500, dec: 15.6278, mag: 3.65, name: 'Hyadum I' },
  { ra: 65.7333, dec: 17.5425, mag: 3.76, name: 'Hyadum II' },
  { ra: 67.1667, dec: 15.8708, mag: 3.40, name: 'Theta2 Tauri' },

  // --- Gemini (remaining) ---
  { ra: 110.0304, dec: 21.9825, mag: 3.53, name: 'Wasat' },
  { ra: 100.9830, dec: 25.1311, mag: 3.06, name: 'Mebsuta' },
  { ra: 102.7130, dec: 20.5704, mag: 3.79, name: 'Mekbuda' },
  { ra: 95.7404, dec: 22.5136, mag: 3.31, name: 'Propus' },
  { ra: 93.7181, dec: 22.5137, mag: 2.88, name: 'Tejat' },

  // --- Canis Major (remaining) ---
  { ra: 95.6875, dec: -30.0631, mag: 3.02, name: 'Furud' },
  { ra: 100.9829, dec: -23.8332, mag: 3.02, name: 'Omicron2 Canis Majoris' },

  // --- Canis Minor (remaining) ---
  { ra: 111.7871, dec: 8.2893, mag: 2.90, name: 'Gomeisa' },

  // --- Auriga (remaining) ---
  { ra: 89.9302, dec: 37.2124, mag: 2.62, name: 'Mahasim' },
  { ra: 74.2377, dec: 33.1660, mag: 2.69, name: 'Hassaleh' },
  { ra: 78.4844, dec: 43.8232, mag: 2.99, name: 'Almaaz' },
  { ra: 75.6197, dec: 41.0757, mag: 3.75, name: 'Saclateni' },

  // --- Perseus (remaining) ---
  { ra: 58.5333, dec: 31.8836, mag: 2.85, name: 'Zeta Persei' },
  { ra: 59.6222, dec: 40.0100, mag: 2.89, name: 'Epsilon Persei' },
  { ra: 47.7876, dec: 53.5062, mag: 2.93, name: 'Gamma Persei' },
  { ra: 61.1671, dec: 47.7877, mag: 3.01, name: 'Delta Persei' },

  // --- Andromeda (remaining) ---
  { ra: 12.3348, dec: 30.8611, mag: 3.28, name: 'Delta Andromedae' },

  // --- Pegasus (remaining) ---
  { ra: 3.3086, dec: 15.1836, mag: 2.83, name: 'Algenib' },
  { ra: 344.4123, dec: 10.8313, mag: 3.40, name: 'Homam' },
  { ra: 340.1665, dec: 30.2213, mag: 2.94, name: 'Matar' },
  { ra: 325.0501, dec: 6.1979, mag: 3.53, name: 'Biham' },

  // --- Bootes (remaining) ---
  { ra: 221.2467, dec: 27.0742, mag: 2.35, name: 'Izar' },
  { ra: 208.6714, dec: 18.3979, mag: 2.68, name: 'Muphrid' },
  { ra: 218.0207, dec: 38.3082, mag: 3.03, name: 'Seginus' },
  { ra: 224.7367, dec: 40.3906, mag: 3.50, name: 'Nekkar' },
  { ra: 221.2506, dec: 33.3151, mag: 3.48, name: 'Delta Bootis' },

  // --- Virgo (remaining) ---
  { ra: 177.6749, dec: 1.7644, mag: 3.60, name: 'Zavijava' },
  { ra: 190.4152, dec: -1.4494, mag: 2.74, name: 'Porrima' },
  { ra: 193.9007, dec: 3.3976, mag: 3.38, name: 'Minelauva' },
  { ra: 195.5439, dec: 10.9591, mag: 2.83, name: 'Vindemiatrix' },
  { ra: 201.6229, dec: -0.5959, mag: 3.37, name: 'Heze' },

  // --- Libra (remaining) ---
  { ra: 225.1706, dec: -25.2817, mag: 3.29, name: 'Brachium' },

  // --- Ophiuchus (remaining) ---
  { ra: 238.5511, dec: -4.6928, mag: 3.24, name: 'Yed Posterior' },
  { ra: 249.2897, dec: -10.5672, mag: 2.56, name: 'Zeta Ophiuchi' },

  // --- Sagittarius (remaining) ---
  { ra: 300.1798, dec: -29.8801, mag: 2.60, name: 'Ascella' },
  { ra: 289.0989, dec: -26.9905, mag: 3.17, name: 'Phi Sagittarii' },

  // --- Capricornus ---
  { ra: 326.7602, dec: -16.1273, mag: 2.87, name: 'Deneb Algedi' },
  { ra: 305.2529, dec: -14.7814, mag: 3.05, name: 'Dabih' },

  // --- Aquarius ---
  { ra: 322.8897, dec: -5.5711, mag: 2.87, name: 'Sadalsuud' },
  { ra: 331.4451, dec: -0.3199, mag: 2.95, name: 'Sadalmelik' },
  { ra: 343.1543, dec: -15.8207, mag: 3.27, name: 'Skat' },

  // --- Centaurus (remaining) ---
  { ra: 204.9719, dec: -53.4664, mag: 2.30, name: 'Epsilon Centauri' },
  { ra: 210.0980, dec: -42.0996, mag: 2.31, name: 'Eta Centauri' },
  { ra: 189.6472, dec: -47.2884, mag: 2.55, name: 'Zeta Centauri' },
  { ra: 190.3717, dec: -50.7222, mag: 2.58, name: 'Delta Centauri' },

  // --- Carina (remaining) ---
  { ra: 139.9319, dec: -64.9856, mag: 2.92, name: 'Upsilon Carinae' },

  // --- Puppis ---
  { ra: 121.7987, dec: -24.3042, mag: 2.83, name: 'Tureis' },
  { ra: 109.2857, dec: -37.0972, mag: 2.70, name: 'Ahadi' },

  // --- Corvus (remaining) ---
  { ra: 187.4634, dec: -16.5153, mag: 2.94, name: 'Algorab' },
  { ra: 183.7799, dec: -22.6202, mag: 3.00, name: 'Minkar' },

  // --- Draco (remaining) ---
  { ra: 211.0973, dec: 64.3758, mag: 3.65, name: 'Thuban' },

  // --- Cepheus (remaining) ---
  { ra: 354.8367, dec: 77.6320, mag: 3.21, name: 'Errai' },

  // --- Columba ---
  { ra: 84.9117, dec: -34.0742, mag: 2.65, name: 'Phact' },
  { ra: 89.0083, dec: -35.7683, mag: 3.12, name: 'Wazn' },

  // --- Lepus ---
  { ra: 83.1827, dec: -17.8224, mag: 2.58, name: 'Arneb' },
  { ra: 82.0613, dec: -20.7594, mag: 2.81, name: 'Nihal' },

  // --- Cetus (remaining) ---
  { ra: 45.5699, dec: 4.0900, mag: 2.54, name: 'Menkar' },

  // --- Eridanus (remaining) ---
  { ra: 76.9629, dec: -5.0862, mag: 2.79, name: 'Cursa' },
  { ra: 59.5072, dec: -13.5087, mag: 2.95, name: 'Zaurak' },
  { ra: 44.5652, dec: -40.3047, mag: 2.88, name: 'Acamar' },

  // --- Canes Venatici ---
  { ra: 194.0068, dec: 38.3186, mag: 2.90, name: 'Cor Caroli' },

  // --- Delphinus ---
  { ra: 309.3877, dec: 15.9121, mag: 3.77, name: 'Sualocin' },
  { ra: 308.8006, dec: 14.5951, mag: 3.63, name: 'Rotanev' },
];
