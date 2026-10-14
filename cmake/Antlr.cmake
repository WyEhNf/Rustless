# The generator and runtime are pinned together; normal builds stay offline.
include(FetchContent)
set(WITH_DEMO OFF CACHE BOOL "" FORCE)
set(ANTLR_BUILD_CPP_TESTS OFF CACHE BOOL "" FORCE)
set(ANTLR_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ANTLR_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(DISABLE_WARNINGS ON CACHE BOOL "" FORCE)
FetchContent_Declare(antlr4_runtime
  URL "${CMAKE_CURRENT_LIST_DIR}/../vendor/antlr/antlr4-cpp-runtime-4.13.2-source.zip"
  URL_HASH SHA256=0ed13668906e86dbc0dcddf30fdee68c10203dea4e83852b4edb810821bee3c4
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(antlr4_runtime)

set(GENERATED_DIR "${CMAKE_CURRENT_LIST_DIR}/../generated/antlr")
file(READ "${GENERATED_DIR}/manifest.json" RX_GENERATED_MANIFEST)
string(JSON RX_ANTLR_VERSION GET "${RX_GENERATED_MANIFEST}" antlr_version)
if(NOT RX_ANTLR_VERSION STREQUAL "4.13.2")
  message(FATAL_ERROR "Generated parser must use ANTLR 4.13.2")
endif()
foreach(RX_GRAMMAR Lexer Parser)
  string(TOLOWER "${RX_GRAMMAR}" RX_GRAMMAR_KEY)
  set(RX_GRAMMAR_PATH "${CMAKE_CURRENT_LIST_DIR}/../grammar/${RX_GRAMMAR}.g4")
  file(READ "${RX_GRAMMAR_PATH}" RX_GRAMMAR_TEXT)
  string(REPLACE "\r\n" "\n" RX_GRAMMAR_TEXT "${RX_GRAMMAR_TEXT}")
  string(SHA256 RX_GRAMMAR_HASH "${RX_GRAMMAR_TEXT}")
  string(JSON RX_GENERATED_HASH GET "${RX_GENERATED_MANIFEST}" "${RX_GRAMMAR_KEY}_sha256")
  if(NOT RX_GRAMMAR_HASH STREQUAL RX_GENERATED_HASH)
    message(FATAL_ERROR
      "${RX_GRAMMAR}.g4 changed: run python scripts/generate_parser.py --antlr-jar /path/to/antlr-4.13.2-complete.jar")
  endif()
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${RX_GRAMMAR_PATH}")
endforeach()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${GENERATED_DIR}/manifest.json")
