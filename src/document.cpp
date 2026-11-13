#include "document.h"

extern "C" const TSLanguage *tree_sitter_phh(void);

#include <algorithm>
#include <cstring>

namespace phh {

namespace {

Position point_to_position(TSPoint p) {
  Position pos;
  pos.line = p.row;
  pos.character = p.column;
  return pos;
}

Range node_range(TSNode node) {
  Range r;
  r.start = point_to_position(ts_node_start_point(node));
  r.end = point_to_position(ts_node_end_point(node));
  return r;
}

std::string node_text(TSNode node, const std::string &source) {
  uint32_t start = ts_node_start_byte(node);
  uint32_t end = ts_node_end_byte(node);
  if (end <= start || end > source.size()) return {};
  return source.substr(start, end - start);
}

// A basic_string/literal_string node's raw text always has one quote
// character at each end.
std::string strip_quotes(const std::string &raw) {
  if (raw.size() >= 2) return raw.substr(1, raw.size() - 2);
  return raw;
}

ValueKind classify_value_node(TSNode node) {
  const char *type = ts_node_type(node);
  if (!std::strcmp(type, "string")) return ValueKind::String;
  if (!std::strcmp(type, "boolean")) return ValueKind::Boolean;
  if (!std::strcmp(type, "integer")) return ValueKind::Integer;
  if (!std::strcmp(type, "float")) return ValueKind::Float;
  if (!std::strcmp(type, "local_time")) return ValueKind::LocalTime;
  if (!std::strcmp(type, "array")) return ValueKind::Array;
  return ValueKind::Unknown;
}

// A `string` node's only named child is a basic_string or literal_string.
std::string string_node_value(TSNode string_node, const std::string &source) {
  return strip_quotes(node_text(string_node, source));
}

void collect_array_elements(TSNode array_node, const std::string &source,
                             std::vector<Field::ArrayElement> &out) {
  uint32_t count = ts_node_named_child_count(array_node);
  for (uint32_t i = 0; i < count; ++i) {
    TSNode child = ts_node_named_child(array_node, i);
    Field::ArrayElement el;
    el.kind = classify_value_node(child);
    el.range = node_range(child);
    if (el.kind == ValueKind::String) {
      el.string_value = string_node_value(child, source);
    }
    out.push_back(std::move(el));
  }
}

Field build_field(TSNode pair_node, const std::string &source) {
  Field field;
  TSNode key_node = ts_node_child_by_field_name(pair_node, "key", 3);
  TSNode value_node = ts_node_child_by_field_name(pair_node, "value", 5);

  field.key = node_text(key_node, source);
  field.key_range = node_range(key_node);
  field.value_range = node_range(value_node);
  field.value_kind = classify_value_node(value_node);

  switch (field.value_kind) {
  case ValueKind::String:
    field.string_value = string_node_value(value_node, source);
    break;
  case ValueKind::Array:
    collect_array_elements(value_node, source, field.array_elements);
    break;
  default:
    break;
  }
  return field;
}

void collect_pairs_into(TSNode parent, const std::string &source, std::vector<Field> &out) {
  uint32_t count = ts_node_named_child_count(parent);
  for (uint32_t i = 0; i < count; ++i) {
    TSNode child = ts_node_named_child(parent, i);
    if (!std::strcmp(ts_node_type(child), "pair")) {
      out.push_back(build_field(child, source));
    }
  }
}

void collect_error_ranges(TSNode node, std::vector<Range> &out) {
  if (ts_node_is_error(node) || ts_node_is_missing(node)) {
    out.push_back(node_range(node));
    return; // don't descend into an error subtree's own (likely garbage) children
  }
  uint32_t count = ts_node_child_count(node);
  for (uint32_t i = 0; i < count; ++i) {
    collect_error_ranges(ts_node_child(node, i), out);
  }
}

} // namespace

DocumentParser::DocumentParser() {
  parser_ = ts_parser_new();
  ts_parser_set_language(parser_, tree_sitter_phh());
}

DocumentParser::~DocumentParser() { ts_parser_delete(parser_); }

ParsedDocument DocumentParser::parse(const std::string &source) const {
  ParsedDocument result;

  TSTree *tree = ts_parser_parse_string(parser_, nullptr, source.c_str(),
                                         static_cast<uint32_t>(source.size()));
  TSNode root = ts_tree_root_node(tree);

  if (ts_node_has_error(root)) {
    result.has_syntax_error = true;
    collect_error_ranges(root, result.syntax_error_ranges);
  }

  uint32_t count = ts_node_named_child_count(root);
  std::vector<Field> leading_pairs;
  bool saw_table = false;

  for (uint32_t i = 0; i < count; ++i) {
    TSNode child = ts_node_named_child(root, i);
    const char *type = ts_node_type(child);

    if (!std::strcmp(type, "pair")) {
      // A `pair` after a `table` can't happen: the grammar makes pairs
      // children of their table.
      if (!saw_table) {
        leading_pairs.push_back(build_field(child, source));
      }
    } else if (!std::strcmp(type, "table")) {
      saw_table = true;
      Hand hand;
      TSNode name_node = ts_node_child_by_field_name(child, "name", 4);
      hand.table_key = node_text(name_node, source);
      hand.range = node_range(child);
      collect_pairs_into(child, source, hand.fields);
      result.hands.push_back(std::move(hand));
    }
  }

  if (!saw_table) {
    // A plain .phh file: the whole document is one implicit hand.
    Hand hand;
    hand.range = node_range(root);
    hand.fields = std::move(leading_pairs);
    result.hands.push_back(std::move(hand));
  }

  ts_tree_delete(tree);
  return result;
}

} // namespace phh
