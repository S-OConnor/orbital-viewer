// test_toml.cpp — olv::toml::parse: happy path + every rejected construct.
// No OLV_TEST_MAIN(); it lives in test_protocol.cpp.

#include <string>

#include "olv/toml.hpp"
#include "olv_test.hpp"

using namespace olv::toml;

namespace {

// Joins lines with CRLF, mirroring how the happy-path test exercises
// CRLF line endings end to end.
std::string crlfJoin(std::initializer_list<std::string> lines) {
  std::string out;
  for (const std::string& l : lines) {
    out += l;
    out += "\r\n";
  }
  return out;
}

}  // namespace

// ---------------------------------------------------------------------------
// Happy path.
// ---------------------------------------------------------------------------

OLV_TEST(toml_happy_path) {
  const std::string text = crlfJoin({
      "root_str = \"hello\"   # a root string",
      "root_int = -5",
      "",
      "[table1]",
      "a_str = \"line1\\nline2 \\\"quoted\\\" back\\\\slash\"  # escapes",
      "a_bool = true",
      "a_float = 3.25",
      "",
      "[table2]",
      "b_int = 42",
      "b_bool = false",
  });

  const ParseResult res = parse(text);
  OLV_CHECK(res.ok());
  OLV_CHECK_EQ(res.error_line, 0);

  OLV_CHECK_EQ(res.values.count("root_str"), 1u);
  OLV_CHECK(res.values.at("root_str").type == Value::Type::kString);
  OLV_CHECK_EQ(res.values.at("root_str").s, std::string("hello"));

  OLV_CHECK(res.values.at("root_int").type == Value::Type::kInteger);
  OLV_CHECK_EQ(res.values.at("root_int").i, -5);

  OLV_CHECK(res.values.at("table1.a_str").type == Value::Type::kString);
  OLV_CHECK_EQ(res.values.at("table1.a_str").s, std::string("line1\nline2 \"quoted\" back\\slash"));

  OLV_CHECK(res.values.at("table1.a_bool").type == Value::Type::kBoolean);
  OLV_CHECK_EQ(res.values.at("table1.a_bool").b, true);

  OLV_CHECK(res.values.at("table1.a_float").type == Value::Type::kFloat);
  OLV_CHECK_NEAR(res.values.at("table1.a_float").f, 3.25, 1e-9);

  OLV_CHECK(res.values.at("table2.b_int").type == Value::Type::kInteger);
  OLV_CHECK_EQ(res.values.at("table2.b_int").i, 42);

  OLV_CHECK(res.values.at("table2.b_bool").type == Value::Type::kBoolean);
  OLV_CHECK_EQ(res.values.at("table2.b_bool").b, false);
}

OLV_TEST(toml_table_reopen_allowed_unless_full_key_repeats) {
  const std::string text =
      "[a]\n"
      "x = 1\n"
      "[b]\n"
      "y = 1\n"
      "[a]\n"
      "z = 3\n";
  const ParseResult res = parse(text);
  OLV_CHECK(res.ok());
  OLV_CHECK_EQ(res.values.at("a.x").i, 1);
  OLV_CHECK_EQ(res.values.at("b.y").i, 1);
  OLV_CHECK_EQ(res.values.at("a.z").i, 3);
}

// ---------------------------------------------------------------------------
// Error cases.
// ---------------------------------------------------------------------------

OLV_TEST(toml_error_unterminated_string) {
  const ParseResult res = parse("x = \"abc");
  OLV_CHECK(!res.ok());
  OLV_CHECK_EQ(res.error_line, 1);
  OLV_CHECK(res.error.find("unterminated string") != std::string::npos);
}

OLV_TEST(toml_error_bad_escape) {
  const ParseResult res = parse("x = \"abc\\qdef\"");
  OLV_CHECK(!res.ok());
  OLV_CHECK_EQ(res.error_line, 1);
  OLV_CHECK(res.error.find("unsupported escape") != std::string::npos);
}

OLV_TEST(toml_error_array_value) {
  const ParseResult res = parse("x = [1, 2]");
  OLV_CHECK(!res.ok());
  OLV_CHECK_EQ(res.error_line, 1);
  OLV_CHECK(res.error.find("arrays") != std::string::npos);
}

OLV_TEST(toml_error_inline_table) {
  const ParseResult res = parse("x = {a = 1}");
  OLV_CHECK(!res.ok());
  OLV_CHECK_EQ(res.error_line, 1);
  OLV_CHECK(res.error.find("inline tables") != std::string::npos);
}

OLV_TEST(toml_error_single_quoted_string) {
  const ParseResult res = parse("x = 'abc'");
  OLV_CHECK(!res.ok());
  OLV_CHECK_EQ(res.error_line, 1);
  OLV_CHECK(res.error.find("single-quoted") != std::string::npos);
}

OLV_TEST(toml_error_dotted_key) {
  const ParseResult res = parse("a.b = 1");
  OLV_CHECK(!res.ok());
  OLV_CHECK_EQ(res.error_line, 1);
  OLV_CHECK(res.error.find("dotted keys") != std::string::npos);
}

OLV_TEST(toml_error_duplicate_full_key) {
  const std::string text =
      "[a]\n"
      "x = 1\n"
      "x = 2\n";
  const ParseResult res = parse(text);
  OLV_CHECK(!res.ok());
  OLV_CHECK_EQ(res.error_line, 3);
  OLV_CHECK(res.error.find("duplicate key") != std::string::npos);
  OLV_CHECK(res.error.find("a.x") != std::string::npos);
}

OLV_TEST(toml_error_missing_equals) {
  const ParseResult res = parse("x 1");
  OLV_CHECK(!res.ok());
  OLV_CHECK_EQ(res.error_line, 1);
  OLV_CHECK(res.error.find("expected '='") != std::string::npos);
}

OLV_TEST(toml_error_missing_value) {
  const ParseResult res = parse("x =   ");
  OLV_CHECK(!res.ok());
  OLV_CHECK_EQ(res.error_line, 1);
  OLV_CHECK(res.error.find("missing value") != std::string::npos);
}

OLV_TEST(toml_error_malformed_float_trailing_dot) {
  const ParseResult res = parse("x = 1.");
  OLV_CHECK(!res.ok());
  OLV_CHECK_EQ(res.error_line, 1);
  OLV_CHECK(res.error.find("decimal point") != std::string::npos);
}

OLV_TEST(toml_error_nan) {
  const ParseResult res = parse("x = nan");
  OLV_CHECK(!res.ok());
  OLV_CHECK_EQ(res.error_line, 1);
  OLV_CHECK(res.error.find("unsupported value") != std::string::npos);
}

OLV_TEST(toml_error_hex_number) {
  const ParseResult res = parse("x = 0x10");
  OLV_CHECK(!res.ok());
  OLV_CHECK_EQ(res.error_line, 1);
  OLV_CHECK(res.error.find("unsupported value") != std::string::npos);
}

OLV_TEST(toml_error_underscore_number) {
  const ParseResult res = parse("x = 1_000");
  OLV_CHECK(!res.ok());
  OLV_CHECK_EQ(res.error_line, 1);
  OLV_CHECK(res.error.find("unsupported value") != std::string::npos);
}

OLV_TEST(toml_error_empty_table_name) {
  const ParseResult res = parse("[]");
  OLV_CHECK(!res.ok());
  OLV_CHECK_EQ(res.error_line, 1);
  OLV_CHECK(res.error.find("empty table name") != std::string::npos);
}

OLV_TEST(toml_error_text_after_table_header) {
  const ParseResult res = parse("[a] extra");
  OLV_CHECK(!res.ok());
  OLV_CHECK_EQ(res.error_line, 1);
  OLV_CHECK(res.error.find("after table header") != std::string::npos);
}

OLV_TEST(toml_error_text_after_value) {
  const ParseResult res = parse("x = 1 extra");
  OLV_CHECK(!res.ok());
  OLV_CHECK_EQ(res.error_line, 1);
  OLV_CHECK(res.error.find("after value") != std::string::npos);
}
