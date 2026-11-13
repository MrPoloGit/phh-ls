#pragma once

#include "document.h"
#include "registry.h"

#include <string>
#include <vector>

namespace phh {

enum class Severity { Error, Warning, Information, Hint };

struct Diagnostic {
  Range range;
  Severity severity = Severity::Error;
  std::string code; // short rule id, e.g. "missing-required-field"
  std::string message;
};

// Registry-driven checks against an already-parsed document: required
// fields per variant, exclusive-group conflicts, unrecognized field names,
// per-player array-length consistency, and (via actions_validate.h)
// action-notation grammar and short-deck card legality. Does not re-check
// TOML-level syntax; that's ParsedDocument::has_syntax_error.
std::vector<Diagnostic> validate(const ParsedDocument &doc, const Registry &registry);

} // namespace phh
