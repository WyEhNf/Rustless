cmake_minimum_required(VERSION 3.24)
project(rustless_frontend LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(RX_JUDGE_BUILD "Reduce cold build time for the judge" OFF)

include(cmake/Antlr.cmake)

add_library(rustless_frontend STATIC
  "${GENERATED_DIR}/RxLexer.cpp"
  "${GENERATED_DIR}/RxParser.cpp"
  src/ast/ast.cpp
  src/ast/frontend.cpp
  src/llvm/ir/ir.cpp
  src/llvm/bridge/facts.cpp
  src/llvm/lower.cpp
  src/llvm/lowering/function.cpp
  src/llvm/lowering/storage.cpp
  src/llvm/lowering/expressions.cpp
  src/llvm/lowering/adjustments.cpp
  src/llvm/lowering/operators.cpp
  src/llvm/lowering/assignment.cpp
  src/llvm/lowering/branches.cpp
  src/llvm/lowering/loops.cpp
  src/llvm/lowering/aggregates.cpp
  src/llvm/lowering/projections.cpp
  src/llvm/lowering/calls.cpp
  src/llvm/builtins/builtins.cpp
  src/llvm/builtins/vectors.cpp
  src/llvm/builtins/clone.cpp
  src/llvm/builtins/equality.cpp
  src/llvm/runtime/runtime.cpp
  src/llvm/runtime/runtime_input.cpp
  src/llvm/runtime/runtime_output.cpp
  src/llvm/ir/ir_print.cpp
  src/llvm/ir/ir_verify.cpp
  src/llvm/ir/ir_builder.cpp
  src/llvm/bridge/ir_semantic.cpp
  src/llvm/bridge/ir_operators.cpp
  src/llvm/bridge/ir_call.cpp
  src/semantic/semantic_core.cpp
  src/semantic/resolution/collection.cpp
  src/semantic/resolution/declarations.cpp
  src/semantic/resolution/structures.cpp
  src/semantic/checking/functions.cpp
  src/semantic/checking/exprs.cpp
  src/semantic/checking/expr_flow.cpp
  src/semantic/checking/expr_values.cpp
  src/semantic/checking/expr_control.cpp
  src/semantic/checking/expr_operators.cpp
  src/semantic/checking/calls.cpp
  src/semantic/checking/method_calls.cpp
  src/semantic/resolution/type_resolution.cpp
  src/semantic/resolution/constants.cpp
  src/semantic/checking/coercion.cpp
  src/semantic/model/type.cpp
  src/semantic/model/operation.cpp
  src/semantic/result.cpp
  src/semantic/dump.cpp
  src/semantic/safety/safe_gate.cpp
  src/semantic/safety/checks.cpp
  src/semantic/safety/links_gate.cpp
  src/semantic/safety/tree_gate.cpp
  src/semantic/safety/declarations_gate.cpp
  src/semantic/safety/shapes_gate.cpp)
target_include_directories(rustless_frontend PUBLIC include "${GENERATED_DIR}")
target_include_directories(rustless_frontend PRIVATE src)
target_include_directories(rustless_frontend SYSTEM PRIVATE
  "${antlr4_runtime_SOURCE_DIR}/runtime/src")
target_link_libraries(rustless_frontend PUBLIC antlr4_static)
target_compile_definitions(rustless_frontend PRIVATE ANTLR4CPP_STATIC)

add_executable(rxc src/main.cpp)
target_link_libraries(rxc PRIVATE rustless_frontend)

if(RX_JUDGE_BUILD)
  foreach(RX_BUILD_TARGET antlr4_static rustless_frontend)
    target_precompile_headers(${RX_BUILD_TARGET} PRIVATE
      <algorithm>
      <functional>
      <map>
      <memory>
      <optional>
      <set>
      <string>
      <unordered_map>
      <unordered_set>
      <variant>
      <vector>)
  endforeach()
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(antlr4_static PRIVATE -O1)
    target_compile_options(rustless_frontend PRIVATE -O0)
    target_compile_options(rxc PRIVATE -O0)
  endif()
endif()

include(CTest)
if(BUILD_TESTING)
  add_executable(ir_lowering tests/ir_lowering.cpp)
  target_link_libraries(ir_lowering PRIVATE rustless_frontend)
  add_test(NAME ir_lowering COMMAND ir_lowering)
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
    add_test(NAME rxoj_runner
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/rxoj_runner_test.py")
    find_program(RX_CLANG NAMES clang-22 clang)
    if(RX_CLANG)
      add_test(NAME llvm_cli
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/llvm_cli_test.py"
          "$<TARGET_FILE:rxc>" "${RX_CLANG}")
    else()
      add_test(NAME llvm_cli
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/llvm_cli_test.py"
          "$<TARGET_FILE:rxc>")
    endif()
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
  if(Python3_Interpreter_FOUND AND
     EXISTS "${CMAKE_SOURCE_DIR}/tests/official/semantic/arrays/manifest.json")
    add_test(NAME semantic_official
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/scripts/test_frontend.py"
        --compiler "$<TARGET_FILE:rxc>"
        --stage semantic
        --test-root "${CMAKE_SOURCE_DIR}/tests/official/semantic"
        --report "${CMAKE_BINARY_DIR}/semantic-official-report.json")
  endif()
  add_executable(semantic_smoke tests/semantic_smoke.cpp)
  target_link_libraries(semantic_smoke PRIVATE rustless_frontend)
  add_test(NAME semantic_smoke COMMAND semantic_smoke)
endif()
