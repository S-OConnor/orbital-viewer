// toml.js — minimal TOML-subset parser, mirrors backend/include/olv/toml.hpp.
//
// Kept deliberately small and dependency-free (see docs/PLAN.md §10); no DOM
// access so it is importable from Node tests as well as the browser.
//
// Supported subset (identical to the C++ parser):
//   - comments (# to end of line) and blank lines
//   - one level of tables: [name]; keys before any table live at the root
//   - bare keys: [A-Za-z0-9_-]+
//   - values: double-quoted strings (escapes: \" \\ \n \t \r), booleans
//     (true/false), integers ([+-]?digits, no underscores), floats
//     ([+-]?digits.digits, no exponent)
// Rejected with a line-numbered error (NOT silently ignored): arrays,
// inline tables, dotted keys, single-quoted/multi-line strings, dates,
// hex/oct/bin numbers, underscores in numbers, duplicate keys.
//
// Result keys are flattened to "table.key" (or "key" at the root); values
// are plain strings/numbers/booleans.

function isBareKeyChar(c) {
  return /[A-Za-z0-9_-]/.test(c);
}

function isSpace(c) {
  return c === ' ' || c === '\t';
}

function skipSpace(s, i) {
  while (i < s.length && isSpace(s[i])) ++i;
  return i;
}

// True when s[i..] holds only whitespace or a comment.
function onlyTrailing(s, i) {
  i = skipSpace(s, i);
  return i >= s.length || s[i] === '#';
}

// Parses a double-quoted string starting at s[i] === '"'. Returns
// { value, next } on success, or throws { message } (caller adds line info).
function parseQuotedString(s, i) {
  let out = '';
  ++i; // opening quote
  while (i < s.length) {
    const c = s[i];
    if (c === '"') {
      return { value: out, next: i + 1 };
    }
    if (c === '\\') {
      if (i + 1 >= s.length) {
        throw new Error('unterminated escape sequence');
      }
      const e = s[i + 1];
      switch (e) {
        case '"':
          out += '"';
          break;
        case '\\':
          out += '\\';
          break;
        case 'n':
          out += '\n';
          break;
        case 't':
          out += '\t';
          break;
        case 'r':
          out += '\r';
          break;
        default:
          throw new Error(`unsupported escape "\\${e}"`);
      }
      i += 2;
      continue;
    }
    out += c;
    ++i;
  }
  throw new Error('unterminated string');
}

// Parses an unquoted scalar token (boolean or number).
function parseScalar(token) {
  if (token === 'true' || token === 'false') {
    return token === 'true';
  }
  if (token === '') {
    throw new Error('missing value');
  }
  let hasDot = false;
  let hasDigit = false;
  for (let i = 0; i < token.length; ++i) {
    const c = token[i];
    if (c === '+' || c === '-') {
      if (i !== 0) {
        throw new Error('sign only allowed at the start of a number');
      }
    } else if (c === '.') {
      if (hasDot) {
        throw new Error('more than one decimal point');
      }
      const prev = token[i - 1];
      const next = token[i + 1];
      if (i === 0 || i + 1 >= token.length || !/[0-9]/.test(prev || '') || !/[0-9]/.test(next || '')) {
        throw new Error('decimal point must have digits on both sides');
      }
      hasDot = true;
    } else if (/[0-9]/.test(c)) {
      hasDigit = true;
    } else {
      throw new Error(`unsupported value "${token}" (subset allows strings, booleans, integers, floats)`);
    }
  }
  if (!hasDigit) {
    throw new Error(`unsupported value "${token}"`);
  }
  if (hasDot) {
    const v = Number(token);
    if (!Number.isFinite(v)) {
      throw new Error(`invalid float "${token}"`);
    }
    return v;
  }
  const v = Number(token);
  if (!Number.isSafeInteger(v)) {
    throw new Error(`invalid integer "${token}"`);
  }
  return v;
}

/**
 * parseToml(text) -> plain object mapping flattened keys ("key" or
 * "table.key") to string|number|boolean. Throws `Error("line N: message")`
 * on any syntax the C++ subset in olv/toml.hpp also rejects.
 */
export function parseToml(text) {
  // Built as a Map and converted with Object.fromEntries() at the end (which
  // uses CreateDataProperty, not [[Set]]) so a bare key literally named
  // "__proto__" cannot reassign the resulting object's prototype.
  const values = new Map();
  let table = '';
  let ln = 0;

  const normalized = text.replace(/\r\n/g, '\n');
  const lines = normalized.split('\n');
  // A trailing '\n' produces one extra empty element from split(); drop it
  // so we don't process a phantom final line (mirrors std::getline, which
  // does not yield a final empty line for input ending in '\n').
  if (lines.length > 0 && lines[lines.length - 1] === '' && normalized.endsWith('\n')) {
    lines.pop();
  }

  const fail = (msg) => {
    throw new Error(`line ${ln}: ${msg}`);
  };

  for (const rawLine of lines) {
    ++ln;
    // CRLF already normalized above; still strip a lone trailing '\r' just
    // in case (mirrors the C++ pop_back check, defensive for odd input).
    const line = rawLine.endsWith('\r') ? rawLine.slice(0, -1) : rawLine;

    let i = skipSpace(line, 0);
    if (i >= line.length || line[i] === '#') continue;

    if (line[i] === '[') {
      const close = line.indexOf(']', i);
      if (close === -1) fail("missing ']' in table header");
      const name = line.slice(i + 1, close);
      if (name === '') fail('empty table name');
      for (const c of name) {
        if (!isBareKeyChar(c)) {
          fail(`invalid table name "${name}" (bare names only; no dotted tables)`);
        }
      }
      if (!onlyTrailing(line, close + 1)) {
        fail('unexpected text after table header');
      }
      table = name;
      continue;
    }

    // key = value
    const keyStart = i;
    while (i < line.length && isBareKeyChar(line[i])) ++i;
    const key = line.slice(keyStart, i);
    if (key === '') fail('expected a key (bare keys: letters, digits, \'_\', \'-\')');
    i = skipSpace(line, i);
    if (i >= line.length || line[i] !== '=') {
      if (i < line.length && line[i] === '.') fail('dotted keys are not supported');
      fail(`expected '=' after key "${key}"`);
    }
    i = skipSpace(line, i + 1);
    if (i >= line.length || line[i] === '#') fail(`missing value for key "${key}"`);

    let value;
    const first = line[i];
    try {
      if (first === '"') {
        const parsed = parseQuotedString(line, i);
        value = parsed.value;
        i = parsed.next;
      } else if (first === '[' || first === '{') {
        throw new Error('arrays and inline tables are not supported');
      } else if (first === "'") {
        throw new Error('single-quoted strings are not supported');
      } else {
        const tokStart = i;
        while (i < line.length && !isSpace(line[i]) && line[i] !== '#') ++i;
        value = parseScalar(line.slice(tokStart, i));
      }
    } catch (err) {
      fail(err.message);
    }
    if (!onlyTrailing(line, i)) fail('unexpected text after value');

    const full = table === '' ? key : `${table}.${key}`;
    if (values.has(full)) {
      fail(`duplicate key "${full}"`);
    }
    values.set(full, value);
  }

  return Object.fromEntries(values);
}
