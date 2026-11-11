cmake_minimum_required(VERSION 3.24)
option(SEQUEL_PREFER_CLANG "Prefer clang/clang++ when no compiler was selected" ON)
if(SEQUEL_PREFER_CLANG AND NOT DEFINED CMAKE_C_COMPILER AND NOT CMAKE_GENERATOR MATCHES "Visual Studio")
  find_program(SEQUEL_CLANG clang)
  find_program(SEQUEL_CLANGXX clang++)
  if(SEQUEL_CLANG AND SEQUEL_CLANGXX)
    set(CMAKE_C_COMPILER "${SEQUEL_CLANG}" CACHE FILEPATH "C compiler")
    set(CMAKE_CXX_COMPILER "${SEQUEL_CLANGXX}" CACHE FILEPATH "C++ compiler")
  endif()
endif()
project(sequel VERSION 0.2.0 LANGUAGES C CXX)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# Keep Clang's Windows AddressSanitizer runtime beside local sanitizer builds.
if(WIN32 AND CMAKE_C_COMPILER_ID MATCHES "Clang" AND
   CMAKE_C_FLAGS MATCHES "(^| )-fsanitize=address( |$)")
  execute_process(
    COMMAND "${CMAKE_C_COMPILER}" -print-file-name=clang_rt.asan_dynamic-x86_64.dll
    OUTPUT_VARIABLE SEQUEL_ASAN_RUNTIME
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
  if(NOT EXISTS "${SEQUEL_ASAN_RUNTIME}")
    execute_process(
      COMMAND "${CMAKE_C_COMPILER}" -print-file-name=clang_rt.asan_dynamic-i386.dll
      OUTPUT_VARIABLE SEQUEL_ASAN_RUNTIME
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET)
  endif()
  if(NOT EXISTS "${SEQUEL_ASAN_RUNTIME}")
    message(FATAL_ERROR "Could not locate the Clang AddressSanitizer runtime")
  endif()

  function(sequel_copy_asan_runtime target)
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${SEQUEL_ASAN_RUNTIME}" "$<TARGET_FILE_DIR:${target}>"
      VERBATIM)
  endfunction()
endif()
option(SEQUEL_BUILD_DOCS "Enable API documentation and publication targets" OFF)
if(SEQUEL_BUILD_DOCS)
  include(docs/targets.cmake)
endif()
option(SEQUEL_BUILD_TESTS "Build sequel tests" ${PROJECT_IS_TOP_LEVEL})
option(SEQUEL_BUILD_CLI "Build the sequel executable" ${PROJECT_IS_TOP_LEVEL})
option(SEQUEL_STATIC_CLI "Statically link the CLI engine and Windows CRT (Linux also links libc statically)" OFF)
if(SEQUEL_STATIC_CLI AND BUILD_SHARED_LIBS)
  message(FATAL_ERROR "SEQUEL_STATIC_CLI requires BUILD_SHARED_LIBS=OFF")
endif()
if(SEQUEL_STATIC_CLI AND MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set(SEQUEL_UTF8PROC_TARGET "" CACHE STRING "Existing utf8proc target (optional)")
if(SEQUEL_UTF8PROC_TARGET)
  if(NOT TARGET "${SEQUEL_UTF8PROC_TARGET}")
    message(FATAL_ERROR "SEQUEL_UTF8PROC_TARGET must name an existing target")
  endif()
else()
  add_library(sequel_utf8proc STATIC third_party/utf8proc/utf8proc.c)
  target_include_directories(sequel_utf8proc PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/third_party/utf8proc>)
  target_compile_definitions(sequel_utf8proc PUBLIC UTF8PROC_STATIC)
  set_target_properties(sequel_utf8proc PROPERTIES POSITION_INDEPENDENT_CODE ON)
  set(SEQUEL_UTF8PROC_TARGET sequel_utf8proc)
endif()
add_library(sequel lib/engine.c lib/runtime.c lib/diagnostics.c lib/contracts.c lib/sha256.c lib/highlight.c lib/compact.c lib/api.c lib/inspection.c lib/inspection_document.c lib/inspection_text.c lib/bindings.c lib/statements.c lib/joins.c lib/expressions.c lib/cte.c lib/tokens.c lib/analysis/analysis.c lib/analysis/format.c lib/analysis/summary.c lib/analysis/layout.c lib/analysis/locations.c lib/analysis/operation.c lib/analysis/lifecycle.c lib/analysis/analysis_literal.c lib/analysis/analysis_binding_token.c lib/analysis/analysis_text.c lib/analysis/analysis_policy.c lib/source.c lib/dialect.c lib/lexer.c lib/parser.c lib/query.c lib/query_arrays.c lib/analysis/document.c lib/serialization.c lib/formatter.c lib/format_buffer.c lib/format_rules.c lib/fingerprint.c lib/summary/entry.c lib/summary/render.c lib/summary/document.c lib/summary/render_document.c lib/summary/routes.c lib/summary/special.c lib/summary/recovery.c lib/summary/normalize.c lib/summary/buffer.c lib/summary/options.c lib/summary/lexical.c lib/summary/operation.c lib/summary/clause.c lib/summary/body.c lib/summary/relation.c lib/summary/postprocess.c lib/summary/statement.c lib/summary/subquery.c lib/sanitizer.c lib/util/utf8.c)
target_sources(sequel PRIVATE lib/analysis/maps.c)
target_sources(sequel PRIVATE lib/analysis/statements.c)
add_library(sequel::sequel ALIAS sequel)
target_sources(sequel PRIVATE lib/access.c)
target_compile_features(sequel PUBLIC c_std_23)
target_include_directories(sequel PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(sequel PRIVATE ${SEQUEL_UTF8PROC_TARGET})
set_target_properties(sequel PROPERTIES POSITION_INDEPENDENT_CODE ON
  WINDOWS_EXPORT_ALL_SYMBOLS ON VERSION ${PROJECT_VERSION} SOVERSION 0)

option(SEQUEL_BUILD_CXX "Build the C++2d facade" ON)
if(SEQUEL_BUILD_CXX)
  include(CheckCXXCompilerFlag)
  if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC" AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(SEQUEL_CXX_DRAFT_FLAG "/clang:-std=c++2d")
  else()
    set(SEQUEL_CXX_DRAFT_FLAG "-std=c++2d")
  endif()
  check_cxx_compiler_flag("${SEQUEL_CXX_DRAFT_FLAG}" SEQUEL_HAS_CXX2D)
  if(NOT SEQUEL_HAS_CXX2D)
    message(FATAL_ERROR "The C++ facade requires a compiler supporting C++2d (for example, Clang 23 or newer)")
  endif()
  add_library(sequel_cxx cpp/sequel.cpp)
  add_library(sequel::cxx ALIAS sequel_cxx)
  target_compile_options(sequel_cxx PUBLIC "$<$<COMPILE_LANGUAGE:CXX>:${SEQUEL_CXX_DRAFT_FLAG}>")
  target_include_directories(sequel_cxx PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/cpp/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
  target_link_libraries(sequel_cxx PUBLIC sequel::sequel)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND NOT MSVC)
    find_library(SEQUEL_LIBCXX NAMES c++ libc++ libcxx)
    if(SEQUEL_LIBCXX)
      target_compile_options(sequel_cxx PUBLIC -stdlib=libc++)
      target_link_options(sequel_cxx INTERFACE -stdlib=libc++)
    endif()
  endif()
  set_target_properties(sequel_cxx PROPERTIES POSITION_INDEPENDENT_CODE ON WINDOWS_EXPORT_ALL_SYMBOLS ON EXPORT_NAME cxx)
endif()
if(SEQUEL_BUILD_CLI)
  add_executable(sequel-cli src/main.c src/embedded.c src/diagnostics.c src/interactive.c)
  target_link_libraries(sequel-cli PRIVATE sequel::sequel)
  set_target_properties(sequel-cli PROPERTIES OUTPUT_NAME sequel)
  if(WIN32)
    target_link_libraries(sequel-cli PRIVATE shell32)
  endif()
  if(SEQUEL_STATIC_CLI AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_options(sequel-cli PRIVATE -static)
  endif()
  if(COMMAND sequel_copy_asan_runtime)
    sequel_copy_asan_runtime(sequel-cli)
  endif()
  install(TARGETS sequel-cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
if(SEQUEL_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
install(TARGETS sequel EXPORT sequelTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(TARGET sequel_utf8proc)
  install(TARGETS sequel_utf8proc EXPORT sequelTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
endif()
install(FILES include/sequel.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY include/sequel DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
if(SEQUEL_BUILD_CXX)
  install(TARGETS sequel_cxx EXPORT sequelTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  install(DIRECTORY cpp/include/sequel DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
endif()
install(FILES LICENSE third_party/utf8proc/LICENSE.md third_party/BASE16_LICENSE.md DESTINATION ${CMAKE_INSTALL_DATADIR}/sequel)
install(EXPORT sequelTargets NAMESPACE sequel:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/sequel)
configure_package_config_file(cmake/sequelConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/sequelConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/sequel)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/sequelConfigVersion.cmake
  VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/sequelConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/sequelConfigVersion.cmake DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/sequel)
