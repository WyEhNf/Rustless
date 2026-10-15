cmake_minimum_required(VERSION 3.24)
project(rustless_frontend LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(cmake/Antlr.cmake)

add_library(rustless_frontend STATIC
  "${GENERATED_DIR}/RxLexer.cpp"
  "${GENERATED_DIR}/RxParser.cpp"
  src/ast/ast.cpp
  src/ast/frontend.cpp
  src/llvm/ir.cpp
  src/llvm/facts.cpp
  src/llvm/lower.cpp
  src/llvm/function.cpp
  src/llvm/storage.cpp
  src/llvm/expressions.cpp
  src/llvm/adjustments.cpp
  src/llvm/operators.cpp
  src/llvm/assignment.cpp
  src/llvm/branches.cpp
  src/llvm/loops.cpp
  src/llvm/aggregates.cpp
  src/llvm/projections.cpp
  src/llvm/calls.cpp
  src/llvm/builtins.cpp
  src/llvm/vectors.cpp
  src/llvm/clone.cpp
  src/llvm/equality.cpp
  src/llvm/runtime.cpp
  src/llvm/runtime_input.cpp
  src/llvm/runtime_output.cpp
  src/llvm/ir_print.cpp
  src/llvm/ir_verify.cpp
  src/llvm/ir_builder.cpp
  src/llvm/ir_semantic.cpp
  src/llvm/ir_operators.cpp
  src/llvm/ir_call.cpp
  src/semantic/semantic_core.cpp
  src/semantic/collection.cpp
  src/semantic/declarations.cpp
  src/semantic/structures.cpp
  src/semantic/functions.cpp
  src/semantic/exprs.cpp
  src/semantic/expr_flow.cpp
  src/semantic/expr_values.cpp
  src/semantic/expr_control.cpp
  src/semantic/expr_operators.cpp
  src/semantic/calls.cpp
  src/semantic/method_calls.cpp
  src/semantic/type_resolution.cpp
  src/semantic/constants.cpp
  src/semantic/coercion.cpp
  src/semantic/type.cpp
  src/semantic/operation.cpp
  src/semantic/result.cpp
  src/semantic/dump.cpp
  src/semantic/safety/safe_gate.cpp
  src/semantic/safety/checks.cpp
  src/semantic/safety/links_gate.cpp
  src/semantic/safety/tree_gate.cpp
  src/semantic/safety/declarations_gate.cpp
  src/semantic/safety/shapes_gate.cpp)
target_include_directories(rustless_frontend PUBLIC include "${GENERATED_DIR}")
target_include_directories(rustless_frontend SYSTEM PRIVATE
  "${antlr4_runtime_SOURCE_DIR}/runtime/src")
target_link_libraries(rustless_frontend PUBLIC antlr4_static)
target_compile_definitions(rustless_frontend PRIVATE ANTLR4CPP_STATIC)

add_executable(rxc src/main.cpp)
target_link_libraries(rxc PRIVATE rustless_frontend)

include(CTest)
if(BUILD_TESTING)
  add_executable(semantic_divergence tests/semantic_divergence.cpp)
  target_link_libraries(semantic_divergence PRIVATE rustless_frontend)
  add_test(NAME semantic_divergence COMMAND semantic_divergence)
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
    add_test(NAME offline_inputs
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/offline_inputs_test.py"
        "${CMAKE_COMMAND}" "${CMAKE_GENERATOR}")
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
