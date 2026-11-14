# phh-ls

The single language server every editor talks to: VS Code, Neovim, Vim,
Zed all start this same binary over stdio and speak standard LSP to it.
Written in C++, built on the grammars from
`../tree-sitter-phh` and `../tree-sitter-phh-actions` and the data from
`../phh/registry`.

## Build

```sh
cmake -S . -B build
cmake --build build -j4
```

Works the same way on macOS, Linux and Windows: the only prerequisites
are a C/C++ compiler (Clang/GCC/MSVC) and CMake 3.16+. `CMakeLists.txt`
fetches and builds tree-sitter (the runtime library, not a grammar) and
yyjson from source via CMake's `FetchContent`, pinned to the exact
versions this was built and tested against, so there's no system package
manager, Homebrew formula, or vcpkg port to install first, and no need to
reconcile what each OS happens to package. The first configure needs
network access to clone those two repos; after that they're cached in
`build/_deps` like any other dependency.

Produces two binaries:

- `build/phh-ls <registry-dir>`: the actual LSP server, speaks JSON-RPC
  over stdin/stdout. Point any LSP client at it with the path to
  `../phh/registry` as its one argument.
- `build/phh-check <registry-dir> <file>...`: a standalone CLI that runs
  the exact same validator without any LSP plumbing, for fast iteration and
  the corpus regression check below. Prints one line per diagnostic
  (`file:line:col: severity: code: message`) plus a summary line per file.

## What's implemented

L0 (syntax, via `../tree-sitter-phh`, not re-implemented here) plus L1
(diagnostics, hover, completion), for `.phh` and `.phhs`:

- **Diagnostics** (`src/validate.cpp`, `src/actions_validate.cpp`), all
  driven by `phh/registry/*.json`, never hard-coded:
  - missing required field for the hand's variant
  - exclusive-group conflicts (e.g. `bring_in` set on a blinds-based variant)
  - unrecognized field names (warning, not error; matches PokerKit's own
    leniency, see `phh/registry/fields.json`'s `custom_field_rule`)
  - per-player array-length mismatches (`antes`, `blinds_or_straddles`,
    `players`, `finishing_stacks`, `winnings`, `seats`, `time_banks` vs.
    `starting_stacks`' length)
  - action-notation grammar errors, by actually parsing every `actions`
    string with `tree-sitter-phh-actions` and mapping its error ranges back
    into the host document
  - player-index-out-of-range and short-deck forbidden-rank checks, walking
    the successfully-parsed action tree
- **Hover**: field name → its registry doc; `variant` value → the matched
  variant's full name (or "unknown variant code" if it doesn't match one of
  the 11).
- **Completion**: every required/optional field name and every variant
  code (unfiltered by context; see "Known limitations" below).
- Full-document sync (`textDocumentSync: 1`): `didChange` always carries
  the whole new text, so the server just re-parses and re-validates; no
  incremental-edit tracking to get wrong.

## How this was verified (not just "it compiles")

1. **`phh-check` against the full corpus** (`../phh/corpus/`): all 5
   `valid/` fixtures (including both real hands from `phh-std/examples.rst`
   and both `.phhs` files) produce **zero diagnostics**; all 5 `invalid/`
   fixtures each produce **exactly the one diagnostic** their corpus
   comment and `phh/corpus/README.md` say they should, checked by running
   it, not by reading the code:

   ```
   missing-required-field.phh:4:11: error: missing-required-field: ... requires field "min_bet" ...
   exclusive-field-conflict.phh:9:1: error: exclusive-field-conflict: Field "bring_in" is not valid ...
   bad-action-grammar.phh:13:4: error: bad-action-grammar: Not a valid PHH action notation ...
   short-deck-forbidden-rank.phh:12:12: error: card-illegal-for-deck: Rank "2" ... does not exist in the "short_deck_36" deck ...
   array-length-mismatch.phh:7:9: error: array-length-mismatch: "antes" has length 3, expected 2 ...
   ```

2. **`tests/lsp_smoke_test.py` against the real server binary**, driving it
   over actual Content-Length-framed JSON-RPC the way an editor would,
   not by calling internal functions. Covers `initialize` capabilities,
   `didOpen` → `publishDiagnostics` for every corpus file (asserting zero
   vs. non-zero diagnostics matches `valid/`/`invalid/`), `hover`,
   `completion`, `didChange` re-validation after introducing a new error
   live, `didClose` clearing diagnostics, and a clean `shutdown`/`exit`.
   All checks pass:

   ```sh
   python3 tests/lsp_smoke_test.py build/phh-ls ../phh/registry ../phh/corpus
   ```

## Known limitations

- **Error-node granularity for malformed actions.** When an `actions`
  string fails to parse (e.g. `"p1 raise 300"`, where `raise` isn't a
  recognized verb), the diagnostic's range currently spans from the start
  of that action string rather than pinpointing just the bad token
  (`raise`). The diagnostic is still correct and names the violated rule;
  this is tree-sitter's own error-recovery granularity for that production,
  not a validator bug. Narrowing it would mean restructuring
  `tree-sitter-phh-actions`'s grammar around explicit error-recovery
  points, not done here, since the message is already actionable.
- **Completion is unfiltered by context.** It always returns every field
  name and variant code regardless of cursor position or what variant is
  already set; narrowing this to
  "only fields still missing for this hand's variant" is later work, not
  part of a first server.
- **No go-to-definition, rename, or semantic tokens.** Not part of the L0/L1
  scope this step targeted.
- **No incremental sync, no debounce.** Every `didChange` re-parses and
  re-validates the whole document from scratch. Fine at PHH's actual file
  sizes (a few hundred bytes to a few KB per hand); would need attention
  only for a very large `.phhs` file, which is out of scope until there is
  a real one to measure against.
- **Not yet packaged for any editor.** This step built and verified the
  server itself; VS Code/Neovim/Vim/Zed integration (starting this binary,
  associating `.phh`/`.phhs`, wiring up the TextMate grammar for VS Code)
  is later work, not done here.
