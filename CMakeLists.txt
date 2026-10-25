cmake_minimum_required(VERSION 3.24)
project(sequel VERSION 0.1.0 LANGUAGES C)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
option(SEQUEL_BUILD_TESTS "Build sequel tests" ${PROJECT_IS_TOP_LEVEL})
option(SEQUEL_BUILD_CLI "Build the sequel executable" ${PROJECT_IS_TOP_LEVEL})
set(SEQUEL_UTF8PROC_TARGET "" CACHE STRING "Existing utf8proc target (optional)")
if(SEQUEL_UTF8PROC_TARGET)
  if(NOT TARGET "${SEQUEL_UTF8PROC_TARGET}")
    message(FATAL_ERROR "SEQUEL_UTF8PROC_TARGET must name an existing target")
  endif()
else()
  add_library(sequel_utf8proc STATIC vendor/utf8proc.c)
  target_include_directories(sequel_utf8proc PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/vendor>)
  target_compile_definitions(sequel_utf8proc PUBLIC UTF8PROC_STATIC)
  set_target_properties(sequel_utf8proc PROPERTIES POSITION_INDEPENDENT_CODE ON)
  set(SEQUEL_UTF8PROC_TARGET sequel_utf8proc)
endif()
add_library(sequel src/sql/main.c src/utf8.c)
add_library(sequel::sequel ALIAS sequel)
target_compile_features(sequel PUBLIC c_std_11)
target_include_directories(sequel PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(sequel PRIVATE ${SEQUEL_UTF8PROC_TARGET})
set_target_properties(sequel PROPERTIES POSITION_INDEPENDENT_CODE ON
  WINDOWS_EXPORT_ALL_SYMBOLS ON VERSION ${PROJECT_VERSION} SOVERSION 0)
if(SEQUEL_BUILD_CLI)
  add_executable(sequel-cli src/binary/main.c src/binary/embedded.c)
  target_link_libraries(sequel-cli PRIVATE sequel::sequel)
  set_target_properties(sequel-cli PROPERTIES OUTPUT_NAME sequel)
  if(WIN32)
    target_link_libraries(sequel-cli PRIVATE shell32)
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
install(FILES LICENSE vendor/UTF8PROC_LICENSE.md DESTINATION ${CMAKE_INSTALL_DATADIR}/sequel)
install(EXPORT sequelTargets NAMESPACE sequel:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/sequel)
configure_package_config_file(cmake/sequelConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/sequelConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/sequel)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/sequelConfigVersion.cmake
  VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/sequelConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/sequelConfigVersion.cmake DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/sequel)
