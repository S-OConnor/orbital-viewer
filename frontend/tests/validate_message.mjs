#!/usr/bin/env node
// validate_message.mjs — re-parses captured backend WS frames (one JSON
// object per line, as produced by tools/ws_probe.cpp) with the real
// frontend parser (net.js#parseStateMessage). Proves the backend's JSON is
// frontend-compatible. Invoked by scripts/integration_test.sh:
//
//   node frontend/tests/validate_message.mjs FILE [--min-objects N]
//
// Behavior:
//   - Any line that fails to parse: print "line <n>: <error>" to stderr,
//     exit 1.
//   - Requires at least one 'state' frame with a non-null satellite;
//     otherwise print an error and exit 1.
//   - If --min-objects N is given, requires the maximum objects-per-frame
//     across all state frames to be >= N; otherwise print an error and
//     exit 1.
//   - On success: print a one-line summary (frames, states, max objects)
//     and exit 0.

import { readFileSync } from 'node:fs';
import { parseStateMessage } from '../js/net.js';

function parseArgs(argv) {
  const args = argv.slice(2);
  if (args.length < 1) {
    return { error: 'usage: validate_message.mjs FILE [--min-objects N]' };
  }
  const file = args[0];
  let minObjects = null;
  for (let i = 1; i < args.length; i++) {
    if (args[i] === '--min-objects') {
      const raw = args[i + 1];
      const n = Number(raw);
      if (raw === undefined || !Number.isFinite(n)) {
        return { error: `--min-objects requires a numeric argument, got ${JSON.stringify(raw)}` };
      }
      minObjects = n;
      i += 1;
    }
  }
  return { file, minObjects };
}

function main(argv) {
  const parsedArgs = parseArgs(argv);
  if (parsedArgs.error) {
    console.error(parsedArgs.error);
    process.exit(1);
  }
  const { file, minObjects } = parsedArgs;

  let text;
  try {
    text = readFileSync(file, 'utf8');
  } catch (err) {
    console.error(`validate_message: cannot read ${file}: ${err.message}`);
    process.exit(1);
  }

  const lines = text.split('\n');
  let frameCount = 0;
  let stateCount = 0;
  let sawStateWithSatellite = false;
  let maxObjects = 0;

  for (let i = 0; i < lines.length; i++) {
    const line = lines[i];
    if (line.trim() === '') continue;
    frameCount += 1;
    let parsed;
    try {
      parsed = parseStateMessage(line);
    } catch (err) {
      console.error(`line ${i + 1}: ${err && err.message ? err.message : err}`);
      process.exit(1);
    }
    if (parsed.type === 'state') {
      stateCount += 1;
      if (parsed.satellite !== null) sawStateWithSatellite = true;
      if (parsed.objects.length > maxObjects) maxObjects = parsed.objects.length;
    }
  }

  if (frameCount === 0) {
    console.error('validate_message: no frames found in file');
    process.exit(1);
  }
  if (!sawStateWithSatellite) {
    console.error('validate_message: no state frame with a non-null satellite was found');
    process.exit(1);
  }
  if (minObjects !== null && maxObjects < minObjects) {
    console.error(`validate_message: max objects-per-frame ${maxObjects} is below required --min-objects ${minObjects}`);
    process.exit(1);
  }

  console.log(`frames=${frameCount} states=${stateCount} maxObjects=${maxObjects}`);
  process.exit(0);
}

main(process.argv);
