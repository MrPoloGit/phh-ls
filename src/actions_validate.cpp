#include "actions_validate.h"

extern "C" const TSLanguage *tree_sitter_phh_actions(void);

#include <cstring>

namespace phh {

namespace {

// Maps a point inside the extracted action-string substring back to a
// position in the host document. Action strings are always single-line,
// so every sub-parse point has row 0 and the mapping is just a column
// offset on the same host line.
Position map_point(const Field::ArrayElement &element, TSPoint sub_point) {
  Position pos;
  pos.line = element.range.start.line + sub_point.row;
  pos.character =
      (sub_point.row == 0 ? element.range.start.character + 1 : 0) + sub_point.column;
  return pos;
}

Range map_node_range(const Field::ArrayElement &element, TSNode node) {
  Range r;
  r.start = map_point(element, ts_node_start_point(node));
  r.end = map_point(element, ts_node_end_point(node));
  return r;
}

std::string node_text(TSNode node, const std::string &content) {
  uint32_t start = ts_node_start_byte(node);
  uint32_t end = ts_node_end_byte(node);
  if (end <= start || end > content.size()) return {};
  return content.substr(start, end - start);
}

void collect_grammar_errors(TSNode node, const Field::ArrayElement &element,
                             std::vector<Diagnostic> &out) {
  if (ts_node_is_error(node) || ts_node_is_missing(node)) {
    Diagnostic d;
    d.range = map_node_range(element, node);
    d.severity = Severity::Error;
    d.code = "bad-action-grammar";
    d.message = "Not a valid PHH action notation (phh-std/required.rst \"Actions\"): "
                "expected a dealer action (\"d db ...\", \"d dh pN ...\"), a player action "
                "(\"pN pb\", \"pN cbr <amount>\", \"pN cc\", \"pN f\", \"pN sd [cards]\", "
                "\"pN sm [cards]\"), or a standalone \"# commentary\"/empty no-op.";
    out.push_back(std::move(d));
    return;
  }
  uint32_t count = ts_node_child_count(node);
  for (uint32_t i = 0; i < count; ++i) {
    collect_grammar_errors(ts_node_child(node, i), element, out);
  }
}

// Walks the successfully-parsed tree for the two checks the action grammar
// itself leaves out: player-index range and short-deck card-rank legality.
void collect_semantic_checks(TSNode node, const Field::ArrayElement &element,
                              const std::string &content, const Registry &registry,
                              const std::string &deck_name, int player_count,
                              std::vector<Diagnostic> &out) {
  const char *type = ts_node_type(node);

  if (!std::strcmp(type, "player_ref")) {
    std::string text = node_text(node, content); // "pN"
    int idx = 0;
    for (size_t i = 1; i < text.size(); ++i) {
      if (text[i] < '0' || text[i] > '9') { idx = -1; break; }
      idx = idx * 10 + (text[i] - '0');
    }
    if (player_count > 0 && (idx < 1 || idx > player_count)) {
      Diagnostic d;
      d.range = map_node_range(element, node);
      d.severity = Severity::Error;
      d.code = "player-index-out-of-range";
      d.message = "Player reference \"" + text + "\" is out of range: this hand has " +
                   std::to_string(player_count) +
                   " player(s) (inferred from starting_stacks' length), so valid "
                   "references are p1.." + "p" + std::to_string(player_count) + ".";
      out.push_back(std::move(d));
    }
  } else if (!std::strcmp(type, "card")) {
    std::string text = node_text(node, content); // 2 chars: rank + suit
    if (text.size() == 2 && text[0] != '?') {
      std::string rank(1, text[0]);
      auto deck_it = registry.decks.find(deck_name);
      if (deck_it != registry.decks.end() && deck_it->second.forbidden_ranks.count(rank)) {
        Diagnostic d;
        d.range = map_node_range(element, node);
        d.severity = Severity::Error;
        d.code = "card-illegal-for-deck";
        d.message = "Rank \"" + rank + "\" (card \"" + text + "\") does not exist in the \"" +
                     deck_name +
                     "\" deck used by this hand's variant (phh-std/required.rst: \"When a "
                     "card that does not exist for a particular variant being played is "
                     "specified, the parser should treat this as a violation of the "
                     "specification.\").";
        out.push_back(std::move(d));
      }
    }
  }

  uint32_t count = ts_node_child_count(node);
  for (uint32_t i = 0; i < count; ++i) {
    collect_semantic_checks(ts_node_child(node, i), element, content, registry, deck_name,
                             player_count, out);
  }
}

} // namespace

std::vector<Diagnostic> validate_actions_field(const Field &actions_field,
                                                const Registry &registry,
                                                const std::string &deck_name,
                                                int player_count) {
  std::vector<Diagnostic> diagnostics;
  if (actions_field.value_kind != ValueKind::Array) return diagnostics;

  TSParser *parser = ts_parser_new();
  ts_parser_set_language(parser, tree_sitter_phh_actions());

  for (const auto &element : actions_field.array_elements) {
    if (element.kind != ValueKind::String) continue;
    const std::string &content = element.string_value;

    TSTree *tree = ts_parser_parse_string(parser, nullptr, content.c_str(),
                                           static_cast<uint32_t>(content.size()));
    TSNode root = ts_tree_root_node(tree);

    if (ts_node_has_error(root)) {
      collect_grammar_errors(root, element, diagnostics);
    } else {
      collect_semantic_checks(root, element, content, registry, deck_name, player_count,
                               diagnostics);
    }

    ts_tree_delete(tree);
  }

  ts_parser_delete(parser);
  return diagnostics;
}

} // namespace phh
