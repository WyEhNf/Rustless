cmake_minimum_required(VERSION 3.24)
project(rustless_frontend LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
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
  src/ast.cpp
  src/frontend.cpp)
target_include_directories(rustless_frontend PUBLIC include "${GENERATED_DIR}")
target_include_directories(rustless_frontend SYSTEM PRIVATE
  "${antlr4_runtime_SOURCE_DIR}/runtime/src")
target_link_libraries(rustless_frontend PUBLIC antlr4_static)
target_compile_definitions(rustless_frontend PRIVATE ANTLR4CPP_STATIC)
