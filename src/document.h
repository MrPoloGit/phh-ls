#pragma once

#include <tree_sitter/api.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

// Parses PHH source with tree-sitter-phh into a list of Hands, each a flat
// list of (key, value) Fields with source ranges for diagnostics. The only
// place that walks tree-sitter nodes directly.
namespace phh {

struct Position {
  uint32_t line = 0;      // 0-indexed, matches LSP
  uint32_t character = 0; // 0-indexed UTF-8 byte offset within the line
};

struct Range {
  Position start;
  Position end;
};

enum class ValueKind { String, Boolean, Integer, Float, LocalTime, Array, Unknown };

struct Field {
  std::string key;
  Range key_range;
  ValueKind value_kind = ValueKind::Unknown;
  Range value_range;

  // For String: content with quotes stripped, escapes left as-is (the
  // validator only compares against plain-ASCII tokens like variant codes).
  std::string string_value;

  // For Array: one entry per element. Non-string elements only get `kind`.
  struct ArrayElement {
    ValueKind kind = ValueKind::Unknown;
    Range range;
    std::string string_value;
  };
  std::vector<ArrayElement> array_elements;
};

struct Hand {
  // Table header key, e.g. "1" or "first"; empty for a bare .phh file.
  std::optional<std::string> table_key;
  Range range;
  std::vector<Field> fields;

  const Field *find(const std::string &key) const {
    for (auto &f : fields) {
      if (f.key == key) return &f;
    }
    return nullptr;
  }
};

struct ParsedDocument {
  std::vector<Hand> hands;
  // True if the TOML-level parse hit any ERROR/MISSING node; the validator
  // still runs on a best-effort document rather than giving up.
  bool has_syntax_error = false;
  std::vector<Range> syntax_error_ranges;
};

class DocumentParser {
public:
  DocumentParser();
  ~DocumentParser();
  DocumentParser(const DocumentParser &) = delete;
  DocumentParser &operator=(const DocumentParser &) = delete;

  ParsedDocument parse(const std::string &source) const;

private:
  TSParser *parser_;
};

} // namespace phh
