cmake_minimum_required(VERSION 3.24)
project(rustless_frontend LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
set(WITH_DEMO OFF CACHE BOOL "" FORCE)
set(ANTLR_BUILD_CPP_TESTS OFF CACHE BOOL "" FORCE)
set(ANTLR_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ANTLR_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(DISABLE_WARNINGS ON CACHE BOOL "" FORCE)
FetchContent_Declare(antlr4_runtime
  URL https://www.antlr.org/download/antlr4-cpp-runtime-4.13.2-source.zip
  URL_HASH SHA256=0ed13668906e86dbc0dcddf30fdee68c10203dea4e83852b4edb810821bee3c4
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(antlr4_runtime)

find_package(Java 17 COMPONENTS Runtime REQUIRED)
set(ANTLR_JAR "" CACHE FILEPATH "Path to ANTLR 4.13.2 complete jar")
if(NOT ANTLR_JAR)
  set(ANTLR_JAR "${CMAKE_BINARY_DIR}/antlr-4.13.2-complete.jar")
  file(DOWNLOAD https://www.antlr.org/download/antlr-4.13.2-complete.jar
    "${ANTLR_JAR}"
    EXPECTED_HASH SHA256=eae2dfa119a64327444672aff63e9ec35a20180dc5b8090b7a6ab85125df4d76)
endif()

set(GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
set(ADAPTED_GRAMMAR_DIR "${CMAKE_BINARY_DIR}/grammar")
file(MAKE_DIRECTORY "${GENERATED_DIR}" "${ADAPTED_GRAMMAR_DIR}")

# ANTLR C++ reserves Lexer and Parser, so rename only the build-local copies.
file(READ "${CMAKE_SOURCE_DIR}/grammar/Lexer.g4" RX_LEXER_GRAMMAR)
string(REPLACE "lexer grammar Lexer;" "lexer grammar RxLexer;"
  RX_LEXER_GRAMMAR "${RX_LEXER_GRAMMAR}")
file(WRITE "${ADAPTED_GRAMMAR_DIR}/RxLexer.g4" "${RX_LEXER_GRAMMAR}")
file(READ "${CMAKE_SOURCE_DIR}/grammar/Parser.g4" RX_PARSER_GRAMMAR)
string(REPLACE "parser grammar Parser;" "parser grammar RxParser;"
  RX_PARSER_GRAMMAR "${RX_PARSER_GRAMMAR}")
string(REPLACE "tokenVocab=Lexer" "tokenVocab=RxLexer"
  RX_PARSER_GRAMMAR "${RX_PARSER_GRAMMAR}")
file(WRITE "${ADAPTED_GRAMMAR_DIR}/RxParser.g4" "${RX_PARSER_GRAMMAR}")

add_custom_command(
  OUTPUT "${GENERATED_DIR}/RxLexer.cpp" "${GENERATED_DIR}/RxParser.cpp"
         "${GENERATED_DIR}/RxLexer.h" "${GENERATED_DIR}/RxParser.h"
  COMMAND "${Java_JAVA_EXECUTABLE}" -jar "${ANTLR_JAR}"
    -Dlanguage=Cpp -package rxgrammar -no-listener -Xexact-output-dir
    -o "${GENERATED_DIR}"
    "${ADAPTED_GRAMMAR_DIR}/RxLexer.g4"
    "${ADAPTED_GRAMMAR_DIR}/RxParser.g4"
  DEPENDS grammar/Lexer.g4 grammar/Parser.g4 "${ANTLR_JAR}"
  COMMENT "Generating Rx lexer and parser"
  VERBATIM)

add_library(rustless_frontend STATIC
  "${GENERATED_DIR}/RxLexer.cpp"
  "${GENERATED_DIR}/RxParser.cpp"
  src/ast/ast.cpp
  src/ast/frontend.cpp
  src/llvm/ir.cpp
  src/llvm/ir_print.cpp
  src/llvm/ir_verify.cpp
  src/llvm/ir_builder.cpp
  src/llvm/ir_semantic.cpp
  src/llvm/ir_operators.cpp
  src/llvm/ir_call.cpp
  src/semantic/semantic.cpp
  src/semantic/type.cpp
  src/semantic/operation.cpp
  src/semantic/safety/safe_gate.cpp)
target_include_directories(rustless_frontend PUBLIC include "${GENERATED_DIR}")
target_include_directories(rustless_frontend SYSTEM PRIVATE
  "${antlr4_runtime_SOURCE_DIR}/runtime/src")
target_link_libraries(rustless_frontend PUBLIC antlr4_static)
target_compile_definitions(rustless_frontend PRIVATE ANTLR4CPP_STATIC)

add_executable(rxc src/main.cpp)
target_link_libraries(rxc PRIVATE rustless_frontend)

include(CTest)
if(BUILD_TESTING)
  add_executable(ir_calls tests/ir_calls.cpp)
  target_link_libraries(ir_calls PRIVATE rustless_frontend)
  add_test(NAME ir_calls COMMAND ir_calls)
  add_executable(safe_gate_invariants tests/safe_gate_invariants.cpp)
  target_link_libraries(safe_gate_invariants PRIVATE rustless_frontend)
  add_test(NAME safe_gate_invariants COMMAND safe_gate_invariants)
  add_executable(semantic_operations tests/semantic_operations.cpp)
  target_link_libraries(semantic_operations PRIVATE rustless_frontend)
  add_test(NAME semantic_operations COMMAND semantic_operations)
  add_executable(semantic_type tests/semantic_type.cpp)
  target_link_libraries(semantic_type PRIVATE rustless_frontend)
  add_test(NAME semantic_type COMMAND semantic_type)
  add_executable(ir_semantic tests/ir_semantic.cpp)
  target_link_libraries(ir_semantic PRIVATE rustless_frontend)
  add_test(NAME ir_semantic COMMAND ir_semantic)
  add_executable(ir_alignment tests/ir_alignment.cpp)
  target_link_libraries(ir_alignment PRIVATE rustless_frontend)
  add_test(NAME ir_alignment COMMAND ir_alignment)
  add_executable(semantic_alignment tests/semantic_alignment.cpp)
  target_link_libraries(semantic_alignment PRIVATE rustless_frontend)
  add_test(NAME semantic_alignment COMMAND semantic_alignment)
  add_executable(ast_visitor tests/ast_visitor.cpp)
  target_link_libraries(ast_visitor PRIVATE rustless_frontend)
  add_test(NAME ast_visitor COMMAND ast_visitor)
  add_executable(ir_smoke tests/ir_smoke.cpp)
  target_link_libraries(ir_smoke PRIVATE rustless_frontend)
  add_test(NAME ir_smoke COMMAND ir_smoke)
  add_test(NAME rxc_help COMMAND rxc --help)
  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_Interpreter_FOUND)
    add_test(NAME frontend_cli
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/frontend_cli_test.py"
        "$<TARGET_FILE:rxc>")
  endif()
  add_executable(frontend_ast_smoke tests/frontend_ast_smoke.cpp)
  target_link_libraries(frontend_ast_smoke PRIVATE rustless_frontend)
  add_test(NAME frontend_ast_smoke COMMAND frontend_ast_smoke)
  add_executable(frontend_ast_structure tests/frontend_ast_structure.cpp)
  target_link_libraries(frontend_ast_structure PRIVATE rustless_frontend)
  add_test(NAME frontend_ast_structure COMMAND frontend_ast_structure)
  add_executable(frontend_ast_corpus tests/frontend_ast_corpus.cpp)
  target_link_libraries(frontend_ast_corpus PRIVATE rustless_frontend)
  if(EXISTS "${CMAKE_SOURCE_DIR}/tests/official/semantic/arrays/manifest.json")
    add_test(NAME semantic_official
      COMMAND frontend_ast_corpus "${CMAKE_SOURCE_DIR}/tests/official/semantic"
        --semantic)
  endif()
  add_executable(semantic_smoke tests/semantic_smoke.cpp)
  target_link_libraries(semantic_smoke PRIVATE rustless_frontend)
  add_test(NAME semantic_smoke COMMAND semantic_smoke)
endif()
