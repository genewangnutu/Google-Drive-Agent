// Tests for the pure CLI output formatter gda::cli::FormatFileTable
// (src/cli/commands.cpp). No ToolDispatcher, no FFI, no I/O other than
// reading the golden fixture file -- see Doc/Google-Drive-Agent-Plan.md
// §8.2 and tests/cpp/fixtures/cli_golden/search_table_output.txt.
#include <fstream>
#include <sstream>
#include <string>

// DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN is provided once, centrally, by
// tests/cpp/test_main.cpp -- defining it here too would duplicate main()
// when linked into the shared gda_tests binary.
#include "doctest/doctest.h"
#include "gda/cli/commands.hpp"
#include "nlohmann/json.hpp"

namespace {

std::string ReadFixtureFile(const std::string& relativePath) {
  std::ifstream in(std::string(GDA_TEST_FIXTURES_DIR) + "/" + relativePath, std::ios::binary);
  REQUIRE_MESSAGE(in.good(), "missing fixture: " << relativePath);
  std::ostringstream buf;
  buf << in.rdbuf();
  return buf.str();
}

}  // namespace

TEST_CASE("FormatFileTable renders two files as an aligned table matching the golden file") {
  nlohmann::json files = nlohmann::json::parse(R"([
    {"id": "1abc", "name": "Q3 Report.pdf", "mimeType": "application/pdf"},
    {"id": "2def", "name": "Q3 Report - copy.pdf", "mimeType": "application/pdf"}
  ])");

  std::string actual = gda::cli::FormatFileTable(files);
  std::string expected = ReadFixtureFile("cli_golden/search_table_output.txt");

  CHECK(actual == expected);
}

TEST_CASE("FormatFileTable handles an empty files array") {
  nlohmann::json files = nlohmann::json::array();
  CHECK(gda::cli::FormatFileTable(files) == "(no files)\n");
}

TEST_CASE("FormatFileTable pads columns to the widest cell, not just the header") {
  nlohmann::json files = nlohmann::json::parse(R"([
    {"id": "x", "name": "a", "mimeType": "m"}
  ])");
  // Single short row: column widths should come from the header text
  // ("ID"/"NAME"/"MIMETYPE"), since it is wider than every cell here.
  std::string actual = gda::cli::FormatFileTable(files);
  CHECK(actual == "ID  NAME  MIMETYPE\nx   a     m\n");
}

TEST_CASE("FormatFileTable tolerates missing fields and trims trailing padding") {
  nlohmann::json files = nlohmann::json::parse(R"([{"id": "1abc"}])");
  std::string actual = gda::cli::FormatFileTable(files);
  // name/mimeType are both missing -> "": the row's trailing padding after
  // them is trimmed, leaving just "1abc" and its gutter before NAME's
  // (empty) column.
  CHECK(actual == "ID    NAME  MIMETYPE\n1abc\n");
}
