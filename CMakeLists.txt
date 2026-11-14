cmake_minimum_required(VERSION 3.16)
project(phh-ls LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

# tree-sitter (the runtime library, not a grammar) and yyjson are fetched
# and built from source rather than found via pkg-config, so this builds
# the same way on macOS, Linux and Windows with nothing but a C/C++
# compiler and CMake, no system package manager, Homebrew formula or
# vcpkg port required. Pinned to the exact versions this project has been
# built and tested against (confirmed via `brew list --versions` on the
# machine this was last verified on).
#
# Force a static tree-sitter: its own CMakeLists.txt does
# `option(BUILD_SHARED_LIBS ... ON)`, and `option()` only takes effect if
# the variable isn't already set, so setting it here first (before that
# runs) keeps the final binary self-contained, with no separate .dylib/
# .so/.dll to find at runtime on any of the three platforms.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

include(FetchContent)
FetchContent_Declare(tree_sitter_runtime
  GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter.git
  GIT_TAG v0.27.0
)
FetchContent_Declare(yyjson
  GIT_REPOSITORY https://github.com/ibireme/yyjson.git
  GIT_TAG 0.12.0
)
FetchContent_MakeAvailable(tree_sitter_runtime yyjson)

# The two grammars live in their own sibling repositories. phh-ls builds
# against their generated parser.c directly rather than vendoring a copy.
set(TREE_SITTER_PHH_DIR ${CMAKE_SOURCE_DIR}/../tree-sitter-phh)
set(TREE_SITTER_PHH_ACTIONS_DIR ${CMAKE_SOURCE_DIR}/../tree-sitter-phh-actions)

add_library(grammars STATIC
  ${TREE_SITTER_PHH_DIR}/src/parser.c
  ${TREE_SITTER_PHH_ACTIONS_DIR}/src/parser.c
)
target_include_directories(grammars PUBLIC
  ${TREE_SITTER_PHH_DIR}/src
  ${TREE_SITTER_PHH_ACTIONS_DIR}/src
)

add_executable(phh-ls
  src/registry.cpp
  src/document.cpp
  src/validate.cpp
  src/actions_validate.cpp
  src/jsonrpc.cpp
  src/main.cpp
)

target_include_directories(phh-ls PRIVATE
  ${TREE_SITTER_PHH_DIR}/src
  ${TREE_SITTER_PHH_ACTIONS_DIR}/src
)

target_link_libraries(phh-ls PRIVATE grammars tree-sitter yyjson)

# A standalone CLI entry point for the core validator
add_executable(phh-check
  src/registry.cpp
  src/document.cpp
  src/validate.cpp
  src/actions_validate.cpp
  src/check_main.cpp
)
target_include_directories(phh-check PRIVATE
  ${TREE_SITTER_PHH_DIR}/src
  ${TREE_SITTER_PHH_ACTIONS_DIR}/src
)
target_link_libraries(phh-check PRIVATE grammars tree-sitter yyjson)
