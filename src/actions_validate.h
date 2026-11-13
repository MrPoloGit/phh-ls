#pragma once

#include "document.h"
#include "registry.h"
#include "validate.h"

#include <string>
#include <vector>

// Validates one `actions` array's elements against the action-notation
// micro-grammar (tree-sitter-phh-actions), plus what that grammar leaves
// out: player-index range and short-deck card-rank legality.
namespace phh {

std::vector<Diagnostic> validate_actions_field(const Field &actions_field,
                                                const Registry &registry,
                                                const std::string &deck_name,
                                                int player_count);

} // namespace phh
