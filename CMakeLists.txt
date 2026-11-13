cmake_minimum_required(VERSION 3.16)
project(phh-ls LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(TREE_SITTER REQUIRED tree-sitter)
pkg_check_modules(YYJSON REQUIRED yyjson)

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

target_compile_options(phh-ls PRIVATE ${TREE_SITTER_CFLAGS_OTHER} ${YYJSON_CFLAGS_OTHER})
target_include_directories(phh-ls PRIVATE ${TREE_SITTER_INCLUDE_DIRS} ${YYJSON_INCLUDE_DIRS})
target_link_directories(phh-ls PRIVATE ${TREE_SITTER_LIBRARY_DIRS} ${YYJSON_LIBRARY_DIRS})
target_link_libraries(phh-ls PRIVATE grammars ${TREE_SITTER_LIBRARIES} ${YYJSON_LIBRARIES})

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
target_compile_options(phh-check PRIVATE ${TREE_SITTER_CFLAGS_OTHER} ${YYJSON_CFLAGS_OTHER})
target_include_directories(phh-check PRIVATE ${TREE_SITTER_INCLUDE_DIRS} ${YYJSON_INCLUDE_DIRS})
target_link_directories(phh-check PRIVATE ${TREE_SITTER_LIBRARY_DIRS} ${YYJSON_LIBRARY_DIRS})
target_link_libraries(phh-check PRIVATE grammars ${TREE_SITTER_LIBRARIES} ${YYJSON_LIBRARIES})
