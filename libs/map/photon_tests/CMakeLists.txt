cmake_minimum_required(VERSION 3.22.1)
project(photon_tests LANGUAGES C CXX)

# This small target exercises production parsing and coordination without a map
# download, Qt installation, Android device, or requests to the public server.
set(CMAKE_CXX_STANDARD 23)
get_filename_component(COMAPS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
set(JANSSON_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(JANSSON_BUILD_MAN OFF CACHE BOOL "" FORCE)
set(JANSSON_EXAMPLES OFF CACHE BOOL "" FORCE)
set(JANSSON_INSTALL OFF CACHE BOOL "" FORCE)
set(JANSSON_WITHOUT_TESTS ON CACHE BOOL "" FORCE)
add_subdirectory("${COMAPS_ROOT}/3party/jansson/jansson" jansson)
add_executable(photon_tests
  photon_tests.cpp
  "${COMAPS_ROOT}/libs/map/photon_api.cpp"
  "${COMAPS_ROOT}/libs/coding/url.cpp"
  "${COMAPS_ROOT}/libs/coding/hex.cpp")
target_include_directories(photon_tests PRIVATE
  "${COMAPS_ROOT}" "${COMAPS_ROOT}/libs" "${CMAKE_CURRENT_BINARY_DIR}/jansson/include")
target_link_libraries(photon_tests PRIVATE jansson)
enable_testing()
add_test(NAME photon_tests COMMAND photon_tests)
