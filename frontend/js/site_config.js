// site_config.js — loads optional site-wide defaults from config.toml.
//
// Pure logic module: no DOM access. `fetchFn` is injected so this can be
// unit-tested with a fake and used in the browser with window.fetch.
//
// config.toml is fetched relative to the page at startup and is entirely
// optional — a missing or absent-file deployment is a supported, common
// case (see frontend/config.toml for the documented example). Any failure
// (network error, non-OK response, TOML syntax error, or an individual key
// that doesn't validate) is handled leniently: the frontend must never brick
// on a bad or missing config, unlike the strict backend/simulator TOML
// consumers (backend/include/olv/toml.hpp), which are allowed to fail hard
// on a malformed config file at process startup. Here we only ever log a
// console.warn and drop the offending piece, continuing with whatever
// remains valid.

import { parseToml } from './toml.js';

/**
 * Known config.toml keys, their expected TOML value type, and the
 * loadSiteConfig() result key each maps to. Documented here so the schema
 * has exactly one source of truth; see frontend/config.toml for a worked
 * example of every key.
 */
export const SITE_CONFIG_SCHEMA = {
  'websocket.host': { type: 'string', resultKey: 'host' },
  'websocket.port': { type: 'integer', min: 1, max: 65535, resultKey: 'port' },
  'display.show_trails': { type: 'boolean', resultKey: 'showTrails' },
  'display.trail_seconds': { type: 'integer', min: 0, max: 60, resultKey: 'trailSeconds' },
  'display.show_labels': { type: 'boolean', resultKey: 'showLabels' },
};

function isValidValue(schemaEntry, value) {
  if (schemaEntry.type === 'string') {
    return typeof value === 'string';
  }
  if (schemaEntry.type === 'boolean') {
    return typeof value === 'boolean';
  }
  if (schemaEntry.type === 'integer') {
    return (
      typeof value === 'number' &&
      Number.isInteger(value) &&
      value >= schemaEntry.min &&
      value <= schemaEntry.max
    );
  }
  return false;
}

/**
 * loadSiteConfig(fetchFn) -> plain object with any of
 * {host, port, showTrails, trailSeconds, showLabels} present only when
 * validly configured in config.toml. Never throws; on any failure (fetch
 * rejects, non-OK response, TOML syntax error) resolves to {} silently
 * (missing config is a supported deployment). Individual keys that are
 * unknown or fail validation are skipped with a console.warn each; the rest
 * of a partially-valid file is still used.
 */
export async function loadSiteConfig(fetchFn) {
  let text;
  try {
    const resp = await fetchFn('config.toml');
    if (!resp || !resp.ok) {
      return {};
    }
    text = await resp.text();
  } catch (err) {
    return {};
  }

  let parsed;
  try {
    parsed = parseToml(text);
  } catch (err) {
    console.warn(`config.toml ignored: ${err.message}`);
    return {};
  }

  const result = {};
  for (const [key, value] of Object.entries(parsed)) {
    const schemaEntry = SITE_CONFIG_SCHEMA[key];
    if (!schemaEntry) {
      console.warn(`config.toml: unknown key "${key}" ignored`);
      continue;
    }
    if (!isValidValue(schemaEntry, value)) {
      console.warn(`config.toml: invalid value for key "${key}" ignored`);
      continue;
    }
    result[schemaEntry.resultKey] = value;
  }
  return result;
}
