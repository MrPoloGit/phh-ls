#pragma once

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

// Loads phh-data/registry/*.json
// into lookup structures phh-ls's validator reads at request time. This is
// the one place that knows the registry's on-disk JSON shape; everything
// else works with these structs.
namespace phh {

struct VariantInfo {
  std::string code;
  std::string name;
  bool button_game = false;
  std::string deck; // key into Registry::decks, e.g. "standard52"
  std::vector<std::string> required_fields;
};

struct ExclusiveGroup {
  std::vector<std::string> fields;
  std::string note;
};

struct FieldInfo {
  std::string type;
  std::string doc;
};

struct ActionArgInfo {
  std::string name;
  std::string type;
  bool required = false;
  std::string note;
};

struct ActionInfo {
  std::string code; // "db", "dh", "pb", "cbr", "cc", "f", "sd", "sm"; empty for the no-op entry
  std::string actor; // "dealer" | "player" | ""
  std::string name;
  std::string grammar;
  std::vector<ActionArgInfo> args;
  std::string note;
};

struct DeckInfo {
  int size = 0;
  std::vector<std::string> ranks;
  std::set<std::string> forbidden_ranks;
  std::vector<std::string> suits;
};

// Fields whose value is a per-player array and must have a length equal to
// the number of players. Not itself in the JSON registry; kept here as a
// small constant rather than a redundant boolean on every fields.json entry.
inline const std::set<std::string> &per_player_array_fields() {
  static const std::set<std::string> fields = {
      "antes",      "blinds_or_straddles", "starting_stacks",
      "players",    "finishing_stacks",    "winnings",
      "seats",      "time_banks",
  };
  return fields;
}

class Registry {
public:
  // Loads variants.json, fields.json, actions.json, cards.json from
  // `directory`. Returns false (and leaves `error` set) if any file is
  // missing or malformed. A server that can't read its own rules should
  // refuse to start, not validate against a partial registry.
  bool load(const std::string &directory, std::string &error);

  std::map<std::string, VariantInfo> variants; // keyed by variant code
  std::vector<ExclusiveGroup> exclusive_groups;

  std::map<std::string, FieldInfo> required_field_defs;
  std::map<std::string, FieldInfo> optional_field_defs;

  std::map<std::string, ActionInfo> actions_by_code; // "" key = no-op entry

  std::map<std::string, DeckInfo> decks; // "standard52", "short_deck_36"

  std::optional<VariantInfo> find_variant(const std::string &code) const;

  // True if `field` is required or optional for any variant. Used for the
  // "unrecognized field" warning, which stays field-name-level rather than
  // per-variant.
  bool is_known_field(const std::string &field) const;
};

} // namespace phh
