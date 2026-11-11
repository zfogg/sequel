find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_executable(sequel-probe sql_probe.c)
target_link_libraries(sequel-probe PRIVATE sequel::sequel)
foreach(test IN ITEMS sql sql_sanitize sql_clickhouse sql_format migrated)
  add_test(NAME sequel-${test} COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/${test}_test.py $<TARGET_FILE:sequel-probe>)
endforeach()
add_executable(sequel-api-test sql_format_api_test.c)
target_link_libraries(sequel-api-test PRIVATE sequel::sequel)
add_test(NAME sequel-api COMMAND sequel-api-test)

add_executable(sequel-parser-api-test parser_api_test.c)
target_link_libraries(sequel-parser-api-test PRIVATE sequel::sequel)
add_test(NAME sequel-parser-api COMMAND sequel-parser-api-test)
if(SEQUEL_BUILD_CXX)
  add_executable(sequel-cxx-api-test cxx_api_test.cpp)
  target_link_libraries(sequel-cxx-api-test PRIVATE sequel::cxx)
  add_test(NAME sequel-cxx-api COMMAND sequel-cxx-api-test)
endif()
if(SEQUEL_BUILD_CLI)
  foreach(test IN ITEMS binary embedded inputs)
    add_test(NAME sequel-${test} COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/test_${test}.py $<TARGET_FILE:sequel-cli>)
  endforeach()
endif()

add_executable(sequel-selective-test selective_test.c)
target_link_libraries(sequel-selective-test PRIVATE sequel::sequel)
add_test(NAME sequel-selective COMMAND sequel-selective-test)

add_executable(sequel-highlight-test highlight_api_test.c)
target_link_libraries(sequel-highlight-test PRIVATE sequel::sequel)
add_test(NAME sequel-highlight-api COMMAND sequel-highlight-test)
add_executable(sequel-diagnostics-test diagnostics_api_test.c)
target_link_libraries(sequel-diagnostics-test PRIVATE sequel::sequel)
add_test(NAME sequel-diagnostics-api COMMAND sequel-diagnostics-test)
if(SEQUEL_BUILD_CLI)
  add_test(NAME sequel-highlight COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/test_highlight.py $<TARGET_FILE:sequel-cli>)
  add_test(NAME sequel-diagnostics COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/test_diagnostics.py $<TARGET_FILE:sequel-cli>)
endif()
if(SEQUEL_BUILD_CLI)
  add_test(NAME sequel-analysis-cli COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/test_analysis_cli.py $<TARGET_FILE:sequel-cli>)
endif()

add_executable(sequel-limits-test limits_api_test.c)
target_link_libraries(sequel-limits-test PRIVATE sequel::sequel)
add_test(NAME sequel-limits COMMAND sequel-limits-test)
# Exercise exact boundaries with the same engine and small test-only constants.
set(SEQUEL_PARSER_SOURCES
  ../lib/source.c
  ../lib/dialect.c
  ../lib/lexer.c
  ../lib/parser.c
  ../lib/query.c
  ../lib/analysis_document.c
  ../lib/serialization.c
  ../lib/formatter.c
  ../lib/fingerprint.c
  ../lib/summary/render.c
  ../lib/summary/normalize.c
  ../lib/summary/buffer.c
  ../lib/summary/options.c
  ../lib/summary/lexical.c
  ../lib/summary/operation.c
  ../lib/summary/clause.c
  ../lib/summary/body.c
  ../lib/summary/relation.c
  ../lib/summary/postprocess.c
  ../lib/summary/statement.c
  ../lib/summary/subquery.c
  ../lib/sanitizer.c
  )

add_executable(sequel-limit-boundary-test limit_boundary_test.c ../lib/engine.c ../lib/util/utf8.c ${SEQUEL_PARSER_SOURCES})
target_include_directories(sequel-limit-boundary-test PRIVATE ../include)
target_link_libraries(sequel-limit-boundary-test PRIVATE ${SEQUEL_UTF8PROC_TARGET})
target_compile_definitions(sequel-limit-boundary-test PRIVATE
  SEQUEL_INPUT_MAX=128 SEQUEL_TEXT_MAX=32 SEQUEL_SUMMARY_MAX=64)
add_test(NAME sequel-limit-boundary COMMAND sequel-limit-boundary-test
  ${CMAKE_CURRENT_BINARY_DIR}/limit-boundary-input.tmp)

add_executable(sequel-contracts-test contracts_api_test.c)
target_link_libraries(sequel-contracts-test PRIVATE sequel::sequel)
add_test(NAME sequel-contracts-api COMMAND sequel-contracts-test)

add_executable(sequel-analysis-test analysis_api_test.c)
target_link_libraries(sequel-analysis-test PRIVATE sequel::sequel)
add_test(NAME sequel-analysis-api COMMAND sequel-analysis-test)

# Test the private streaming digest against standard vectors without exposing it.
add_executable(sequel-sha256-test sha256_api_test.c ../lib/util/utf8.c ${SEQUEL_PARSER_SOURCES})
target_include_directories(sequel-sha256-test PRIVATE ../include)
target_link_libraries(sequel-sha256-test PRIVATE ${SEQUEL_UTF8PROC_TARGET})
add_test(NAME sequel-sha256-api COMMAND sequel-sha256-test)

# Keep the bundled utf8proc malloc/free ABI and scan accounting unchanged when
# the SQL implementation is followed by other sources in cgo's unity build.
add_executable(sequel-unity-test unity_api_test.c ${SEQUEL_PARSER_SOURCES})
target_include_directories(sequel-unity-test PRIVATE ../include ../third_party/utf8proc)
target_compile_definitions(sequel-unity-test PRIVATE SEQUEL_TEST_UNITY UTF8PROC_STATIC)
target_compile_features(sequel-unity-test PRIVATE c_std_11)
add_test(NAME sequel-unity-api COMMAND sequel-unity-test)

add_executable(sequel-layout-probe unity_api_test.c)
target_link_libraries(sequel-layout-probe PRIVATE sequel::sequel ${SEQUEL_UTF8PROC_TARGET})
add_test(NAME sequel-layout-parity COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/test_unity.py $<TARGET_FILE:sequel-unity-test> $<TARGET_FILE:sequel-layout-probe>)

if(WIN32 AND BUILD_SHARED_LIBS)
  get_property(_tests DIRECTORY PROPERTY TESTS)
  foreach(test IN LISTS _tests)
    set_property(TEST ${test} APPEND PROPERTY ENVIRONMENT_MODIFICATION
      "PATH=path_list_prepend:$<TARGET_FILE_DIR:sequel>")
  endforeach()
endif()

# Release binaries must exercise assertions too, rather than silently removing
# the C API checks under NDEBUG.
get_property(_sequel_test_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(_target IN LISTS _sequel_test_targets)
  if(MSVC)
    target_compile_options(${_target} PRIVATE /UNDEBUG)
  else()
    target_compile_options(${_target} PRIVATE -UNDEBUG)
  endif()
endforeach()
