# phh-ls

The single language server every editor talks to.

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
  `../phh-data/registry` as its one argument.
- `build/phh-check <registry-dir> <file>...`: a standalone CLI that runs
  the exact same validator without any LSP plumbing, for fast iteration and
  the corpus regression check below. Prints one line per diagnostic
  (`file:line:col: severity: code: message`) plus a summary line per file.
