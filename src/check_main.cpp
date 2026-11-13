// Standalone CLI for the core validator, independent of the LSP/stdio
// protocol layer. Usage:
//
//   phh-check <registry-dir> <file.phh|file.phhs> [more files...]
//
// Prints one line per diagnostic as `<file>:<line>:<col>: <severity>:
// <code>: <message>` (1-indexed line/col, like a compiler), then a summary
// line per file. Always exits 0; this is a reporting tool, not a CI gate.

#include "document.h"
#include "registry.h"
#include "validate.h"

#include <fstream>
#include <iostream>
#include <sstream>

using namespace phh;

namespace {

const char *severity_name(Severity s) {
  switch (s) {
  case Severity::Error: return "error";
  case Severity::Warning: return "warning";
  case Severity::Information: return "information";
  case Severity::Hint: return "hint";
  }
  return "unknown";
}

std::string read_file(const std::string &path, bool &ok) {
  std::ifstream in(path, std::ios::binary);
  if (!in) { ok = false; return {}; }
  std::ostringstream ss;
  ss << in.rdbuf();
  ok = true;
  return ss.str();
}

} // namespace

int main(int argc, char **argv) {
  if (argc < 3) {
    std::cerr << "usage: phh-check <registry-dir> <file.phh|file.phhs> [more files...]\n";
    return 2;
  }

  Registry registry;
  std::string error;
  if (!registry.load(argv[1], error)) {
    std::cerr << "phh-check: failed to load registry: " << error << "\n";
    return 2;
  }

  DocumentParser parser;

  for (int i = 2; i < argc; ++i) {
    std::string path = argv[i];
    bool ok = false;
    std::string source = read_file(path, ok);
    if (!ok) {
      std::cerr << path << ": error: could not read file\n";
      continue;
    }

    ParsedDocument doc = parser.parse(source);
    auto diagnostics = validate(doc, registry);

    if (doc.has_syntax_error) {
      for (const auto &r : doc.syntax_error_ranges) {
        std::cout << path << ":" << (r.start.line + 1) << ":" << (r.start.character + 1)
                  << ": error: syntax: invalid TOML/PHH syntax\n";
      }
    }
    for (const auto &d : diagnostics) {
      std::cout << path << ":" << (d.range.start.line + 1) << ":"
                << (d.range.start.character + 1) << ": " << severity_name(d.severity) << ": "
                << d.code << ": " << d.message << "\n";
    }

    std::cout << path << ": " << diagnostics.size() << " diagnostic(s), "
              << doc.syntax_error_ranges.size() << " syntax error(s), " << doc.hands.size()
              << " hand(s)\n";
  }

  return 0;
}
