// olv/toml.hpp — minimal TOML-subset parser (header-only, first-party).
//
// Shared by olv_backend and olv_sim for configuration files; the frontend
// mirrors the same subset in frontend/js/toml.js. Kept deliberately small
// (like test/support/olv_test.hpp) so the repo stays free of third-party
// code; see docs/PLAN.md §10.
//
// Supported subset:
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
// Result keys are flattened to "table.key" (or "key" at the root).

#pragma once

#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <istream>
#include <map>
#include <sstream>
#include <string>

namespace olv::toml {

struct Value {
  enum class Type { kString, kInteger, kFloat, kBoolean };
  Type type = Type::kString;
  std::string s;
  std::int64_t i = 0;
  double f = 0.0;
  bool b = false;

  const char* typeName() const {
    switch (type) {
      case Type::kString:
        return "string";
      case Type::kInteger:
        return "integer";
      case Type::kFloat:
        return "float";
      case Type::kBoolean:
        return "boolean";
    }
    return "unknown";
  }
};

struct ParseResult {
  std::map<std::string, Value> values;  // flattened "table.key" -> Value
  std::string error;                    // empty on success
  int error_line = 0;                   // 1-based; 0 when no error
  bool ok() const { return error.empty(); }
};

namespace detail {

inline bool isBareKeyChar(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '-';
}

inline bool isSpace(char c) {
  return c == ' ' || c == '\t';
}

inline std::size_t skipSpace(const std::string& s, std::size_t i) {
  while (i < s.size() && isSpace(s[i])) ++i;
  return i;
}

// True when s[i..] holds only whitespace or a comment.
inline bool onlyTrailing(const std::string& s, std::size_t i) {
  i = skipSpace(s, i);
  return i >= s.size() || s[i] == '#';
}

// Parses a double-quoted string starting at s[i] == '"'. On success sets
// `out`, advances `i` past the closing quote, and returns true.
inline bool parseQuotedString(const std::string& s, std::size_t& i, std::string& out,
                              std::string& err) {
  out.clear();
  ++i;  // opening quote
  while (i < s.size()) {
    char c = s[i];
    if (c == '"') {
      ++i;
      return true;
    }
    if (c == '\\') {
      if (i + 1 >= s.size()) {
        err = "unterminated escape sequence";
        return false;
      }
      char e = s[i + 1];
      switch (e) {
        case '"':
          out.push_back('"');
          break;
        case '\\':
          out.push_back('\\');
          break;
        case 'n':
          out.push_back('\n');
          break;
        case 't':
          out.push_back('\t');
          break;
        case 'r':
          out.push_back('\r');
          break;
        default:
          err = std::string("unsupported escape \"\\") + e + "\"";
          return false;
      }
      i += 2;
      continue;
    }
    out.push_back(c);
    ++i;
  }
  err = "unterminated string";
  return false;
}

// Parses an unquoted scalar token (boolean or number).
inline bool parseScalar(const std::string& token, Value& out, std::string& err) {
  if (token == "true" || token == "false") {
    out.type = Value::Type::kBoolean;
    out.b = (token == "true");
    return true;
  }
  if (token.empty()) {
    err = "missing value";
    return false;
  }
  bool has_dot = false;
  bool has_digit = false;
  for (std::size_t i = 0; i < token.size(); ++i) {
    char c = token[i];
    if (c == '+' || c == '-') {
      if (i != 0) {
        err = "sign only allowed at the start of a number";
        return false;
      }
    } else if (c == '.') {
      if (has_dot) {
        err = "more than one decimal point";
        return false;
      }
      if (i == 0 || i + 1 >= token.size() ||
          !std::isdigit(static_cast<unsigned char>(token[i - 1])) ||
          !std::isdigit(static_cast<unsigned char>(token[i + 1]))) {
        err = "decimal point must have digits on both sides";
        return false;
      }
      has_dot = true;
    } else if (std::isdigit(static_cast<unsigned char>(c)) != 0) {
      has_digit = true;
    } else {
      err =
          "unsupported value \"" + token + "\" (subset allows strings, booleans, integers, floats)";
      return false;
    }
  }
  if (!has_digit) {
    err = "unsupported value \"" + token + "\"";
    return false;
  }
  const char* begin = token.c_str();
  char* end = nullptr;
  errno = 0;
  if (has_dot) {
    double v = std::strtod(begin, &end);
    if (end != begin + token.size() || errno == ERANGE) {
      err = "invalid float \"" + token + "\"";
      return false;
    }
    out.type = Value::Type::kFloat;
    out.f = v;
  } else {
    long long v = std::strtoll(begin, &end, 10);
    if (end != begin + token.size() || errno == ERANGE) {
      err = "invalid integer \"" + token + "\"";
      return false;
    }
    out.type = Value::Type::kInteger;
    out.i = static_cast<std::int64_t>(v);
  }
  return true;
}

}  // namespace detail

inline ParseResult parse(std::istream& in) {
  ParseResult res;
  std::string table;
  std::string line;
  int ln = 0;

  auto fail = [&](const std::string& msg) {
    res.error = msg;
    res.error_line = ln;
    return res;
  };

  while (std::getline(in, line)) {
    ++ln;
    if (!line.empty() && line.back() == '\r') line.pop_back();

    std::size_t i = detail::skipSpace(line, 0);
    if (i >= line.size() || line[i] == '#') continue;

    if (line[i] == '[') {
      std::size_t close = line.find(']', i);
      if (close == std::string::npos) return fail("missing ']' in table header");
      std::string name = line.substr(i + 1, close - i - 1);
      if (name.empty()) return fail("empty table name");
      for (char c : name) {
        if (!detail::isBareKeyChar(c)) {
          return fail("invalid table name \"" + name + "\" (bare names only; no dotted tables)");
        }
      }
      if (!detail::onlyTrailing(line, close + 1)) {
        return fail("unexpected text after table header");
      }
      table = name;
      continue;
    }

    // key = value
    std::size_t key_start = i;
    while (i < line.size() && detail::isBareKeyChar(line[i])) ++i;
    std::string key = line.substr(key_start, i - key_start);
    if (key.empty()) return fail("expected a key (bare keys: letters, digits, '_', '-')");
    i = detail::skipSpace(line, i);
    if (i >= line.size() || line[i] != '=') {
      if (i < line.size() && line[i] == '.') return fail("dotted keys are not supported");
      return fail("expected '=' after key \"" + key + "\"");
    }
    i = detail::skipSpace(line, i + 1);
    if (i >= line.size() || line[i] == '#') return fail("missing value for key \"" + key + "\"");

    Value value;
    std::string err;
    char first = line[i];
    if (first == '"') {
      value.type = Value::Type::kString;
      if (!detail::parseQuotedString(line, i, value.s, err)) return fail(err);
    } else if (first == '[' || first == '{') {
      return fail("arrays and inline tables are not supported");
    } else if (first == '\'') {
      return fail("single-quoted strings are not supported");
    } else {
      std::size_t tok_start = i;
      while (i < line.size() && !detail::isSpace(line[i]) && line[i] != '#') ++i;
      if (!detail::parseScalar(line.substr(tok_start, i - tok_start), value, err)) {
        return fail(err);
      }
    }
    if (!detail::onlyTrailing(line, i)) return fail("unexpected text after value");

    std::string full = table.empty() ? key : table + "." + key;
    if (res.values.count(full) != 0) return fail("duplicate key \"" + full + "\"");
    res.values.emplace(std::move(full), std::move(value));
  }
  return res;
}

inline ParseResult parse(const std::string& text) {
  std::istringstream in(text);
  return parse(in);
}

inline ParseResult parseFile(const std::string& path) {
  std::ifstream in(path);
  if (!in.is_open()) {
    ParseResult res;
    res.error = "cannot open config file: " + path;
    return res;
  }
  return parse(in);
}

}  // namespace olv::toml
