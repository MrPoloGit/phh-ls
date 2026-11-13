#include "registry.h"

#include <yyjson.h>

#include <cstdio>

namespace phh {

namespace {

// Reads one *.json file into a yyjson_doc; the caller owns the returned
// pointer and must yyjson_doc_free it. Returns nullptr on any failure.
yyjson_doc *read_json_file(const std::string &path, std::string &error) {
  yyjson_read_err err;
  yyjson_doc *doc = yyjson_read_file(path.c_str(), YYJSON_READ_NOFLAG, nullptr, &err);
  if (!doc) {
    error = "failed to parse " + path + ": " + (err.msg ? err.msg : "unknown error");
    return nullptr;
  }
  return doc;
}

std::string str_or_empty(yyjson_val *v) {
  const char *s = yyjson_is_str(v) ? yyjson_get_str(v) : nullptr;
  return s ? std::string(s) : std::string();
}

std::vector<std::string> str_array(yyjson_val *arr) {
  std::vector<std::string> out;
  if (!yyjson_is_arr(arr)) return out;
  size_t idx, max;
  yyjson_val *v;
  yyjson_arr_foreach(arr, idx, max, v) { out.push_back(str_or_empty(v)); }
  return out;
}

bool load_variants(yyjson_val *root, Registry &reg, std::string &error) {
  yyjson_val *groups = yyjson_obj_get(root, "exclusive_groups");
  size_t idx, max;
  yyjson_val *g;
  if (yyjson_is_arr(groups)) {
    yyjson_arr_foreach(groups, idx, max, g) {
      ExclusiveGroup eg;
      eg.fields = str_array(yyjson_obj_get(g, "fields"));
      eg.note = str_or_empty(yyjson_obj_get(g, "note"));
      reg.exclusive_groups.push_back(std::move(eg));
    }
  }

  yyjson_val *variants = yyjson_obj_get(root, "variants");
  if (!yyjson_is_arr(variants)) {
    error = "variants.json: missing top-level \"variants\" array";
    return false;
  }
  yyjson_val *v;
  yyjson_arr_foreach(variants, idx, max, v) {
    VariantInfo info;
    info.code = str_or_empty(yyjson_obj_get(v, "code"));
    info.name = str_or_empty(yyjson_obj_get(v, "name"));
    info.button_game = yyjson_get_bool(yyjson_obj_get(v, "button_game"));
    info.deck = str_or_empty(yyjson_obj_get(v, "deck"));
    info.required_fields = str_array(yyjson_obj_get(v, "required_fields"));
    if (info.code.empty()) {
      error = "variants.json: a variant entry is missing its \"code\"";
      return false;
    }
    reg.variants[info.code] = std::move(info);
  }
  return true;
}

void load_field_defs(yyjson_val *obj, std::map<std::string, FieldInfo> &out) {
  if (!yyjson_is_obj(obj)) return;
  size_t idx, max;
  yyjson_val *key, *val;
  yyjson_obj_foreach(obj, idx, max, key, val) {
    FieldInfo info;
    info.type = str_or_empty(yyjson_obj_get(val, "type"));
    info.doc = str_or_empty(yyjson_obj_get(val, "doc"));
    out[str_or_empty(key)] = std::move(info);
  }
}

bool load_fields(yyjson_val *root, Registry &reg, std::string & /*error*/) {
  load_field_defs(yyjson_obj_get(root, "required_fields"), reg.required_field_defs);
  load_field_defs(yyjson_obj_get(root, "optional_fields"), reg.optional_field_defs);
  return true;
}

bool load_actions(yyjson_val *root, Registry &reg, std::string &error) {
  yyjson_val *actions = yyjson_obj_get(root, "actions");
  if (!yyjson_is_arr(actions)) {
    error = "actions.json: missing top-level \"actions\" array";
    return false;
  }
  size_t idx, max;
  yyjson_val *a;
  yyjson_arr_foreach(actions, idx, max, a) {
    ActionInfo info;
    yyjson_val *code = yyjson_obj_get(a, "code");
    info.code = yyjson_is_str(code) ? str_or_empty(code) : std::string(); // null -> "" (no-op entry)
    info.actor = str_or_empty(yyjson_obj_get(a, "actor"));
    info.name = str_or_empty(yyjson_obj_get(a, "name"));
    info.grammar = str_or_empty(yyjson_obj_get(a, "grammar"));
    info.note = str_or_empty(yyjson_obj_get(a, "note"));
    yyjson_val *args = yyjson_obj_get(a, "args");
    size_t aidx, amax;
    yyjson_val *arg;
    if (yyjson_is_arr(args)) {
      yyjson_arr_foreach(args, aidx, amax, arg) {
        ActionArgInfo ai;
        ai.name = str_or_empty(yyjson_obj_get(arg, "name"));
        ai.type = str_or_empty(yyjson_obj_get(arg, "type"));
        ai.required = yyjson_get_bool(yyjson_obj_get(arg, "required"));
        ai.note = str_or_empty(yyjson_obj_get(arg, "note"));
        info.args.push_back(std::move(ai));
      }
    }
    reg.actions_by_code[info.code] = std::move(info);
  }
  return true;
}

bool load_cards(yyjson_val *root, Registry &reg, std::string &error) {
  yyjson_val *decks = yyjson_obj_get(root, "decks");
  if (!yyjson_is_obj(decks)) {
    error = "cards.json: missing top-level \"decks\" object";
    return false;
  }
  size_t idx, max;
  yyjson_val *key, *val;
  yyjson_obj_foreach(decks, idx, max, key, val) {
    DeckInfo info;
    info.size = static_cast<int>(yyjson_get_sint(yyjson_obj_get(val, "size")));
    info.ranks = str_array(yyjson_obj_get(val, "ranks"));
    info.suits = str_array(yyjson_obj_get(val, "suits"));
    for (auto &r : str_array(yyjson_obj_get(val, "forbidden_ranks"))) {
      info.forbidden_ranks.insert(r);
    }
    reg.decks[str_or_empty(key)] = std::move(info);
  }
  return true;
}

} // namespace

bool Registry::load(const std::string &directory, std::string &error) {
  struct FilePart {
    const char *name;
    bool (*loader)(yyjson_val *, Registry &, std::string &);
  };
  const FilePart parts[] = {
      {"variants.json", load_variants},
      {"fields.json", load_fields},
      {"actions.json", load_actions},
      {"cards.json", load_cards},
  };

  for (const auto &part : parts) {
    std::string path = directory + "/" + part.name;
    yyjson_doc *doc = read_json_file(path, error);
    if (!doc) return false;
    yyjson_val *root = yyjson_doc_get_root(doc);
    bool ok = part.loader(root, *this, error);
    yyjson_doc_free(doc);
    if (!ok) return false;
  }
  return true;
}

std::optional<VariantInfo> Registry::find_variant(const std::string &code) const {
  auto it = variants.find(code);
  if (it == variants.end()) return std::nullopt;
  return it->second;
}

bool Registry::is_known_field(const std::string &field) const {
  return required_field_defs.count(field) || optional_field_defs.count(field);
}

} // namespace phh
