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

if(WIN32 AND BUILD_SHARED_LIBS)
  get_property(_tests DIRECTORY PROPERTY TESTS)
  foreach(test IN LISTS _tests)
    set_property(TEST ${test} APPEND PROPERTY ENVIRONMENT_MODIFICATION
      "PATH=path_list_prepend:$<TARGET_FILE_DIR:sequel>")
  endforeach()
endif()
