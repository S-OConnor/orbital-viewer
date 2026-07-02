import test from 'node:test';
import assert from 'node:assert/strict';
import { parseToml } from '../js/toml.js';

function assertThrowsLine(text, line, fragment) {
  assert.throws(
    () => parseToml(text),
    (err) => {
      assert.ok(err instanceof Error, 'throws an Error');
      assert.ok(
        err.message.startsWith(`line ${line}: `),
        `expected message to start with "line ${line}: ", got "${err.message}"`
      );
      assert.ok(
        err.message.includes(fragment),
        `expected message to include "${fragment}", got "${err.message}"`
      );
      return true;
    }
  );
}

test('happy path: comments, blank lines, root + table keys, all value types, negatives', () => {
  const text = [
    '# a leading comment',
    '',
    'name = "orbital"',
    'count = 5',
    'neg_int = -12',
    'ratio = 1.5',
    'neg_ratio = -0.25',
    'enabled = true',
    'disabled = false',
    '',
    '# a table',
    '[websocket]',
    'host = "example.com"',
    'port = 8765 # trailing comment is fine',
    '',
    '[display]',
    'show_trails = false',
  ].join('\n');

  const result = parseToml(text);
  assert.deepEqual(result, {
    name: 'orbital',
    count: 5,
    neg_int: -12,
    ratio: 1.5,
    neg_ratio: -0.25,
    enabled: true,
    disabled: false,
    'websocket.host': 'example.com',
    'websocket.port': 8765,
    'display.show_trails': false,
  });
});

test('handles CRLF line endings', () => {
  const text = '[a]\r\nkey = "x"\r\nnum = 42\r\n';
  assert.deepEqual(parseToml(text), { 'a.key': 'x', 'a.num': 42 });
});

test('string escapes: \\" \\\\ \\n \\t \\r', () => {
  const text = 'v = "a\\"b\\\\c\\nd\\te\\rf"';
  const result = parseToml(text);
  assert.equal(result.v, 'a"b\\c\nd\te\rf');
});

test('bare key charset accepts letters, digits, underscore, hyphen', () => {
  const text = 'a1_b-2 = 1';
  assert.deepEqual(parseToml(text), { 'a1_b-2': 1 });
});

test('table name with no keys yet is still valid (empty table)', () => {
  const text = '[empty]\n[other]\nk = 1\n';
  assert.deepEqual(parseToml(text), { 'other.k': 1 });
});

test('error: unterminated string', () => {
  assertThrowsLine('v = "unterminated', 1, 'unterminated string');
});

test('error: bad escape sequence', () => {
  assertThrowsLine('v = "bad\\qescape"', 1, 'unsupported escape');
});

test('error: unterminated escape at end of string', () => {
  assertThrowsLine('v = "trailing\\', 1, 'unterminated escape sequence');
});

test('error: arrays are rejected', () => {
  assertThrowsLine('v = [1, 2, 3]', 1, 'arrays and inline tables are not supported');
});

test('error: inline tables are rejected', () => {
  assertThrowsLine('v = { a = 1 }', 1, 'arrays and inline tables are not supported');
});

test('error: single-quoted strings are rejected', () => {
  assertThrowsLine("v = 'nope'", 1, 'single-quoted strings are not supported');
});

test('error: dotted keys are rejected', () => {
  assertThrowsLine('a.b = 1', 1, 'dotted keys are not supported');
});

test('error: dotted table names are rejected', () => {
  assertThrowsLine('[a.b]', 1, 'invalid table name');
});

test('error: duplicate flattened key at root', () => {
  assertThrowsLine('a = 1\na = 2\n', 2, 'duplicate key "a"');
});

test('error: duplicate flattened key inside a table', () => {
  assertThrowsLine('[t]\nk = 1\nk = 2\n', 3, 'duplicate key "t.k"');
});

test('error: missing "=" after key', () => {
  assertThrowsLine('key value', 1, "expected '='");
});

test('error: missing value for key', () => {
  assertThrowsLine('key =', 1, 'missing value');
});

test('error: missing value for key (comment only after "=")', () => {
  assertThrowsLine('key = # comment', 1, 'missing value');
});

test('error: malformed number "1."', () => {
  assertThrowsLine('v = 1.', 1, 'decimal point must have digits on both sides');
});

test('error: malformed number "nan"', () => {
  assertThrowsLine('v = nan', 1, 'unsupported value "nan"');
});

test('error: malformed number "0x10"', () => {
  assertThrowsLine('v = 0x10', 1, 'unsupported value "0x10"');
});

test('error: malformed number "1_000" (underscores not supported)', () => {
  assertThrowsLine('v = 1_000', 1, 'unsupported value "1_000"');
});

test('error: empty table name', () => {
  assertThrowsLine('[]', 1, 'empty table name');
});

test('error: invalid table name (non-bare characters)', () => {
  assertThrowsLine('[a b]', 1, 'invalid table name');
});

test('error: trailing text after value', () => {
  assertThrowsLine('v = 1 garbage', 1, 'unexpected text after value');
});

test('error: trailing text after table header', () => {
  assertThrowsLine('[t] garbage', 1, 'unexpected text after table header');
});

test('error: missing closing bracket in table header', () => {
  assertThrowsLine('[t', 1, "missing ']'");
});

test('error line numbers account for preceding valid/blank/comment lines', () => {
  const text = '# comment\na = 1\n\n[t]\nb = 2\nb = 3\n';
  assertThrowsLine(text, 6, 'duplicate key "t.b"');
});
