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
add_executable(sequel-limits-test limits_api_test.c)
target_link_libraries(sequel-limits-test PRIVATE sequel::sequel)
add_test(NAME sequel-limits COMMAND sequel-limits-test)
# Exercise exact boundaries with the same engine and small test-only constants.
add_executable(sequel-limit-boundary-test limit_boundary_test.c ../src/sql/main.c ../src/utf8.c)
target_include_directories(sequel-limit-boundary-test PRIVATE ../include)
target_link_libraries(sequel-limit-boundary-test PRIVATE ${SEQUEL_UTF8PROC_TARGET})
target_compile_definitions(sequel-limit-boundary-test PRIVATE
  SEQUEL_INPUT_MAX=128 SEQUEL_TEXT_MAX=32 SEQUEL_SUMMARY_MAX=64)
add_test(NAME sequel-limit-boundary COMMAND sequel-limit-boundary-test
  ${CMAKE_CURRENT_BINARY_DIR}/limit-boundary-input.tmp)

if(WIN32 AND BUILD_SHARED_LIBS)
  get_property(_tests DIRECTORY PROPERTY TESTS)
  foreach(test IN LISTS _tests)
    set_property(TEST ${test} APPEND PROPERTY ENVIRONMENT_MODIFICATION
      "PATH=path_list_prepend:$<TARGET_FILE_DIR:sequel>")
  endforeach()
endif()
