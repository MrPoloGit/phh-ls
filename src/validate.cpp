#include "validate.h"
#include "actions_validate.h"

#include <algorithm>
#include <sstream>
#include <utility>

namespace phh {

namespace {

std::string join(const std::vector<std::string> &items, const char *sep) {
  std::ostringstream out;
  for (size_t i = 0; i < items.size(); ++i) {
    if (i) out << sep;
    out << items[i];
  }
  return out.str();
}

void check_variant_and_required_fields(const Hand &hand, const Registry &registry,
                                         std::vector<Diagnostic> &out,
                                         std::optional<VariantInfo> &variant_out) {
  const Field *variant_field = hand.find("variant");
  if (!variant_field) {
    Diagnostic d;
    d.range = hand.range;
    d.severity = Severity::Error;
    d.code = "missing-required-field";
    d.message = "Missing required field \"variant\" (phh-std/required.rst: every hand "
                 "must specify its variant code).";
    out.push_back(std::move(d));
    return;
  }

  variant_out = registry.find_variant(variant_field->string_value);
  if (!variant_out) {
    Diagnostic d;
    d.range = variant_field->value_range;
    d.severity = Severity::Error;
    d.code = "unknown-variant";
    d.message = "\"" + variant_field->string_value +
                 "\" is not one of the 11 PHH variant codes (phh-std/required.rst): FT, "
                 "NT, NS, PO, FO/8, F7S, F7S/8, FR, N2L1D, F2L3D, FB.";
    out.push_back(std::move(d));
    return;
  }

  for (const auto &required : variant_out->required_fields) {
    if (required == "variant") continue;
    if (!hand.find(required)) {
      Diagnostic d;
      d.range = variant_field->value_range;
      d.severity = Severity::Error;
      d.code = "missing-required-field";
      d.message = "Variant \"" + variant_out->code + "\" (" + variant_out->name +
                   ") requires field \"" + required +
                   "\", which this hand does not set (phh/registry/variants.json, "
                   "cross-checked against pokerkit.notation.HandHistory.required_field_names).";
      out.push_back(std::move(d));
    }
  }
}

void check_exclusive_groups(const Hand &hand, const Registry &registry,
                             const VariantInfo &variant, std::vector<Diagnostic> &out) {
  for (const auto &group : registry.exclusive_groups) {
    std::vector<std::string> allowed; // fields of this group that ARE required for this variant
    for (const auto &f : group.fields) {
      if (std::find(variant.required_fields.begin(), variant.required_fields.end(), f) !=
          variant.required_fields.end()) {
        allowed.push_back(f);
      }
    }
    for (const auto &f : group.fields) {
      if (std::find(allowed.begin(), allowed.end(), f) != allowed.end()) continue;
      const Field *present = hand.find(f);
      if (!present) continue;
      Diagnostic d;
      d.range = present->key_range;
      d.severity = Severity::Error;
      d.code = "exclusive-field-conflict";
      d.message = "Field \"" + f + "\" is not valid for variant \"" + variant.code +
                   "\": " + group.note +
                   (allowed.empty() ? std::string()
                                    : " This variant uses \"" + join(allowed, "\", \"") +
                                          "\" instead.");
      out.push_back(std::move(d));
    }
  }
}

void check_unrecognized_fields(const Hand &hand, const Registry &registry,
                                std::vector<Diagnostic> &out) {
  for (const auto &field : hand.fields) {
    if (registry.is_known_field(field.key)) continue;
    if (!field.key.empty() && field.key[0] == '_') continue; // user-defined, tolerated silently

    Diagnostic d;
    d.range = field.key_range;
    d.severity = Severity::Warning;
    d.code = "unrecognized-field";
    d.message = "\"" + field.key +
                 "\" is not a recognized PHH field and does not start with \"_\". PokerKit "
                 "accepts it (filed under user_defined_fields) but warns; "
                 "phh-std/style.rst asks user-defined fields to be snake_case with a "
                 "leading underscore, e.g. \"_" + field.key + "\".";
    out.push_back(std::move(d));
  }
}

int infer_player_count(const Hand &hand) {
  const Field *stacks = hand.find("starting_stacks");
  if (!stacks || stacks->value_kind != ValueKind::Array) return -1;
  return static_cast<int>(stacks->array_elements.size());
}

void check_array_lengths(const Hand &hand, int player_count, std::vector<Diagnostic> &out) {
  if (player_count < 0) return; // can't determine the expected count at all

  for (const auto &name : per_player_array_fields()) {
    if (name == "starting_stacks") continue; // this is the count's own source of truth
    const Field *field = hand.find(name);
    if (!field || field->value_kind != ValueKind::Array) continue;
    int actual = static_cast<int>(field->array_elements.size());
    if (actual != player_count) {
      Diagnostic d;
      d.range = field->value_range;
      d.severity = Severity::Error;
      d.code = "array-length-mismatch";
      d.message = "\"" + name + "\" has length " + std::to_string(actual) + ", expected " +
                   std::to_string(player_count) +
                   " (inferred from starting_stacks' length). phh-std/spec.rst \"Position\": "
                   "per-player array fields must have length equal to the number of players.";
      out.push_back(std::move(d));
    }
  }
}

} // namespace

std::vector<Diagnostic> validate(const ParsedDocument &doc, const Registry &registry) {
  std::vector<Diagnostic> out;

  for (const auto &hand : doc.hands) {
    std::optional<VariantInfo> variant;
    check_variant_and_required_fields(hand, registry, out, variant);
    check_unrecognized_fields(hand, registry, out);

    int player_count = infer_player_count(hand);
    check_array_lengths(hand, player_count, out);

    if (variant) {
      check_exclusive_groups(hand, registry, *variant, out);
    }

    if (const Field *actions = hand.find("actions")) {
      std::string deck_name = variant ? variant->deck : std::string();
      auto action_diags =
          validate_actions_field(*actions, registry, deck_name, player_count);
      out.insert(out.end(), action_diags.begin(), action_diags.end());
    }
  }

  return out;
}

} // namespace phh
