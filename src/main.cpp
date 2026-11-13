// phh-ls: a standard-LSP-only server for PHH (.phh / .phhs), over stdio.
// Every editor (VS Code, Neovim, Vim, Zed) talks to this exact binary the
// same way: start it, speak LSP over stdio. Nothing here is PHH-editor-
// specific beyond what the LSP spec already allows (diagnostics, hover,
// completion); no custom request/notification is required for basic
// function.

#include "document.h"
#include "jsonrpc.h"
#include "registry.h"
#include "validate.h"

#include <yyjson.h>

#include <cstdio>
#include <iostream>
#include <map>
#include <string>

using namespace phh;

namespace {

int severity_to_lsp(Severity s) {
  switch (s) {
  case Severity::Error: return 1;
  case Severity::Warning: return 2;
  case Severity::Information: return 3;
  case Severity::Hint: return 4;
  }
  return 1;
}

yyjson_mut_val *position_to_json(yyjson_mut_doc *doc, const Position &p) {
  yyjson_mut_val *obj = yyjson_mut_obj(doc);
  yyjson_mut_obj_add_uint(doc, obj, "line", p.line);
  yyjson_mut_obj_add_uint(doc, obj, "character", p.character);
  return obj;
}

yyjson_mut_val *range_to_json(yyjson_mut_doc *doc, const Range &r) {
  yyjson_mut_val *obj = yyjson_mut_obj(doc);
  yyjson_mut_obj_add_val(doc, obj, "start", position_to_json(doc, r.start));
  yyjson_mut_obj_add_val(doc, obj, "end", position_to_json(doc, r.end));
  return obj;
}

// One in-memory open document: its raw text plus what the server derived
// from it most recently, so hover/completion don't have to re-parse on
// every request.
struct OpenDocument {
  std::string text;
};

class Server {
public:
  Server(Registry registry) : registry_(std::move(registry)) {}

  void run() {
    while (true) {
      auto body = read_message(stdin);
      if (!body) break; // client closed the pipe

      yyjson_doc *doc = yyjson_read(body->data(), body->size(), 0);
      if (!doc) continue; // malformed message; nothing sane to respond with
      yyjson_val *root = yyjson_doc_get_root(doc);
      yyjson_val *method_val = yyjson_obj_get(root, "method");
      yyjson_val *id_val = yyjson_obj_get(root, "id");
      yyjson_val *params = yyjson_obj_get(root, "params");
      std::string method = method_val ? yyjson_get_str(method_val) : "";

      dispatch(method, id_val, params);

      yyjson_doc_free(doc);
      if (should_exit_) break;
    }
  }

private:
  Registry registry_;
  DocumentParser parser_;
  std::map<std::string, OpenDocument> open_documents_;
  bool shutdown_received_ = false;
  bool should_exit_ = false;

  void dispatch(const std::string &method, yyjson_val *id, yyjson_val *params) {
    if (method == "initialize") {
      respond_initialize(id);
    } else if (method == "initialized") {
      // nothing to do
    } else if (method == "textDocument/didOpen") {
      on_did_open(params);
    } else if (method == "textDocument/didChange") {
      on_did_change(params);
    } else if (method == "textDocument/didClose") {
      on_did_close(params);
    } else if (method == "textDocument/hover") {
      on_hover(id, params);
    } else if (method == "textDocument/completion") {
      on_completion(id, params);
    } else if (method == "shutdown") {
      shutdown_received_ = true;
      respond_null(id);
    } else if (method == "exit") {
      should_exit_ = true;
    } else if (id) {
      // An unhandled *request* (has an id) must still get a response, or a
      // spec-compliant client will hang waiting for it; an unhandled
      // *notification* (no id) is simply ignored, as the spec allows.
      respond_method_not_found(id, method);
    }
  }

  // ---- outgoing message helpers -----------------------------------------

  void send(yyjson_mut_doc *doc) {
    char *json = yyjson_mut_write(doc, 0, nullptr);
    if (json) {
      write_message(stdout, json);
      free(json);
    }
    yyjson_mut_doc_free(doc);
  }

  yyjson_mut_doc *new_response(yyjson_val *id, yyjson_mut_val **out_obj) {
    yyjson_mut_doc *doc = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val *obj = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, obj);
    yyjson_mut_obj_add_strcpy(doc, obj, "jsonrpc", "2.0");
    if (id) {
      yyjson_mut_val *id_copy = yyjson_val_mut_copy(doc, id);
      yyjson_mut_obj_add_val(doc, obj, "id", id_copy);
    } else {
      yyjson_mut_obj_add_null(doc, obj, "id");
    }
    *out_obj = obj;
    return doc;
  }

  void respond_null(yyjson_val *id) {
    yyjson_mut_val *obj;
    yyjson_mut_doc *doc = new_response(id, &obj);
    yyjson_mut_obj_add_null(doc, obj, "result");
    send(doc);
  }

  void respond_method_not_found(yyjson_val *id, const std::string &method) {
    yyjson_mut_val *obj;
    yyjson_mut_doc *doc = new_response(id, &obj);
    yyjson_mut_val *err = yyjson_mut_obj_add_obj(doc, obj, "error");
    yyjson_mut_obj_add_int(doc, err, "code", -32601);
    yyjson_mut_obj_add_strcpy(doc, err, "message", ("method not found: " + method).c_str());
    send(doc);
  }

  // ---- lifecycle ---------------------------------------------------------

  void respond_initialize(yyjson_val *id) {
    yyjson_mut_val *obj;
    yyjson_mut_doc *doc = new_response(id, &obj);
    yyjson_mut_val *result = yyjson_mut_obj_add_obj(doc, obj, "result");
    yyjson_mut_val *caps = yyjson_mut_obj_add_obj(doc, result, "capabilities");
    // Full-document sync (kind 1): didChange always carries the whole new
    // text, which keeps the server's re-parse-on-every-change logic simple
    // and is what every editor supports regardless of how it's configured
    // locally.
    yyjson_mut_obj_add_int(doc, caps, "textDocumentSync", 1);
    yyjson_mut_obj_add_true(doc, caps, "hoverProvider");
    yyjson_mut_val *completion = yyjson_mut_obj_add_obj(doc, caps, "completionProvider");
    yyjson_mut_val *trigger = yyjson_mut_arr(doc);
    yyjson_mut_arr_add_strcpy(doc, trigger, "\"");
    yyjson_mut_obj_add_val(doc, completion, "triggerCharacters", trigger);

    yyjson_mut_val *info = yyjson_mut_obj_add_obj(doc, result, "serverInfo");
    yyjson_mut_obj_add_strcpy(doc, info, "name", "phh-ls");
    yyjson_mut_obj_add_strcpy(doc, info, "version", "0.1.0");
    send(doc);
  }

  // ---- document sync + diagnostics ---------------------------------------

  std::string extract_uri(yyjson_val *params) {
    yyjson_val *td = yyjson_obj_get(params, "textDocument");
    yyjson_val *uri = yyjson_obj_get(td, "uri");
    return uri ? yyjson_get_str(uri) : std::string();
  }

  void on_did_open(yyjson_val *params) {
    yyjson_val *td = yyjson_obj_get(params, "textDocument");
    std::string uri = extract_uri(params);
    yyjson_val *text = yyjson_obj_get(td, "text");
    if (uri.empty() || !text) return;
    open_documents_[uri] = OpenDocument{yyjson_get_str(text)};
    publish_diagnostics(uri);
  }

  void on_did_change(yyjson_val *params) {
    std::string uri = extract_uri(params);
    if (uri.empty()) return;
    yyjson_val *changes = yyjson_obj_get(params, "contentChanges");
    if (!yyjson_is_arr(changes) || yyjson_arr_size(changes) == 0) return;
    // Full sync: the last entry in contentChanges is authoritative and
    // carries the entire new document text (no "range" field).
    yyjson_val *last = yyjson_arr_get_last(changes);
    yyjson_val *text = yyjson_obj_get(last, "text");
    if (!text) return;
    open_documents_[uri] = OpenDocument{yyjson_get_str(text)};
    publish_diagnostics(uri);
  }

  void on_did_close(yyjson_val *params) {
    std::string uri = extract_uri(params);
    open_documents_.erase(uri);
    // Clear any diagnostics the client was showing for a now-closed file.
    send_publish_diagnostics(uri, {}, nullptr);
  }

  void publish_diagnostics(const std::string &uri) {
    auto it = open_documents_.find(uri);
    if (it == open_documents_.end()) return;
    ParsedDocument parsed = parser_.parse(it->second.text);
    auto diagnostics = validate(parsed, registry_);
    send_publish_diagnostics(uri, diagnostics, &parsed);
  }

  void send_publish_diagnostics(const std::string &uri, const std::vector<Diagnostic> &diagnostics,
                                 const ParsedDocument *parsed) {
    yyjson_mut_doc *doc = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val *obj = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, obj);
    yyjson_mut_obj_add_strcpy(doc, obj, "jsonrpc", "2.0");
    yyjson_mut_obj_add_strcpy(doc, obj, "method", "textDocument/publishDiagnostics");
    yyjson_mut_val *params = yyjson_mut_obj_add_obj(doc, obj, "params");
    yyjson_mut_obj_add_strcpy(doc, params, "uri", uri.c_str());
    yyjson_mut_val *arr = yyjson_mut_obj_add_arr(doc, params, "diagnostics");

    if (parsed) {
      for (const auto &r : parsed->syntax_error_ranges) {
        yyjson_mut_val *item = yyjson_mut_arr_add_obj(doc, arr);
        yyjson_mut_obj_add_val(doc, item, "range", range_to_json(doc, r));
        yyjson_mut_obj_add_int(doc, item, "severity", 1);
        yyjson_mut_obj_add_strcpy(doc, item, "source", "phh-ls");
        yyjson_mut_obj_add_strcpy(doc, item, "code", "syntax-error");
        yyjson_mut_obj_add_strcpy(doc, item, "message", "Invalid TOML/PHH syntax.");
      }
    }
    for (const auto &d : diagnostics) {
      yyjson_mut_val *item = yyjson_mut_arr_add_obj(doc, arr);
      yyjson_mut_obj_add_val(doc, item, "range", range_to_json(doc, d.range));
      yyjson_mut_obj_add_int(doc, item, "severity", severity_to_lsp(d.severity));
      yyjson_mut_obj_add_strcpy(doc, item, "source", "phh-ls");
      yyjson_mut_obj_add_strcpy(doc, item, "code", d.code.c_str());
      yyjson_mut_obj_add_strcpy(doc, item, "message", d.message.c_str());
    }
    send(doc);
  }

  // ---- hover --------------------------------------------------------------

  // Finds the field in `parsed` whose key or value range contains `pos`, if
  // any, across every hand in the document.
  const Field *field_at(const ParsedDocument &parsed, const Position &pos) {
    auto contains = [&](const Range &r) {
      auto before = [](const Position &a, const Position &b) {
        return a.line < b.line || (a.line == b.line && a.character <= b.character);
      };
      return before(r.start, pos) && before(pos, r.end);
    };
    for (const auto &hand : parsed.hands) {
      for (const auto &f : hand.fields) {
        if (contains(f.key_range) || contains(f.value_range)) return &f;
      }
    }
    return nullptr;
  }

  void on_hover(yyjson_val *id, yyjson_val *params) {
    std::string uri = extract_uri(params);
    yyjson_val *position = yyjson_obj_get(params, "position");
    auto it = open_documents_.find(uri);
    if (it == open_documents_.end() || !position) { respond_null(id); return; }

    Position pos;
    pos.line = static_cast<uint32_t>(yyjson_get_uint(yyjson_obj_get(position, "line")));
    pos.character = static_cast<uint32_t>(yyjson_get_uint(yyjson_obj_get(position, "character")));

    ParsedDocument parsed = parser_.parse(it->second.text);
    const Field *field = field_at(parsed, pos);
    if (!field) { respond_null(id); return; }

    std::string text;
    if (field->key == "variant") {
      auto variant = registry_.find_variant(field->string_value);
      text = variant ? ("**" + variant->code + "**: " + variant->name)
                      : ("Unknown variant code \"" + field->string_value + "\".");
    } else {
      auto req_it = registry_.required_field_defs.find(field->key);
      auto opt_it = registry_.optional_field_defs.find(field->key);
      if (req_it != registry_.required_field_defs.end()) {
        text = "**" + field->key + "** (" + req_it->second.type + ", required for some variants)\n\n" +
               req_it->second.doc;
      } else if (opt_it != registry_.optional_field_defs.end()) {
        text = "**" + field->key + "** (" + opt_it->second.type + ", optional)\n\n" + opt_it->second.doc;
      } else {
        respond_null(id);
        return;
      }
    }

    yyjson_mut_val *obj;
    yyjson_mut_doc *doc = new_response(id, &obj);
    yyjson_mut_val *result = yyjson_mut_obj_add_obj(doc, obj, "result");
    yyjson_mut_val *contents = yyjson_mut_obj_add_obj(doc, result, "contents");
    yyjson_mut_obj_add_strcpy(doc, contents, "kind", "markdown");
    yyjson_mut_obj_add_strcpy(doc, contents, "value", text.c_str());
    send(doc);
  }

  // ---- completion ----------------------------------------------------------

  void on_completion(yyjson_val *id, yyjson_val * /*params*/) {
    // Deliberately simple for a first release: always offer every known
    // field name plus every variant code, letting the editor's own fuzzy
    // filtering narrow it down by what's already typed. Context-sensitive
    // completion (e.g. only showing fields still missing for the variant in
    // play) is listed as later work, not done here.
    yyjson_mut_val *obj;
    yyjson_mut_doc *doc = new_response(id, &obj);
    yyjson_mut_val *result = yyjson_mut_arr(doc);
    yyjson_mut_obj_add_val(doc, obj, "result", result);

    auto add_item = [&](const std::string &label, const std::string &detail, int kind) {
      yyjson_mut_val *item = yyjson_mut_arr_add_obj(doc, result);
      yyjson_mut_obj_add_strcpy(doc, item, "label", label.c_str());
      yyjson_mut_obj_add_strcpy(doc, item, "detail", detail.c_str());
      yyjson_mut_obj_add_int(doc, item, "kind", kind);
    };

    for (const auto &[name, info] : registry_.required_field_defs) {
      add_item(name, info.type, 5 /* Field */);
    }
    for (const auto &[name, info] : registry_.optional_field_defs) {
      add_item(name, info.type, 5 /* Field */);
    }
    for (const auto &[code, info] : registry_.variants) {
      add_item(code, info.name, 21 /* EnumMember */);
    }

    send(doc);
  }
};

} // namespace

int main(int argc, char **argv) {
  if (argc < 2) {
    std::cerr << "usage: phh-ls <registry-dir>\n";
    return 2;
  }

  Registry registry;
  std::string error;
  if (!registry.load(argv[1], error)) {
    std::cerr << "phh-ls: failed to load registry: " << error << "\n";
    return 1;
  }

  Server server(std::move(registry));
  server.run();
  return 0;
}
