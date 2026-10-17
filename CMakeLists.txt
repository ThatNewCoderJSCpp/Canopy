cmake_minimum_required(VERSION 3.18)
project(canopy VERSION 0.1.0 DESCRIPTION "Canopy: a code editor built with Grove" LANGUAGES C CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(CANOPY_WITH_WALNUT "Build Walnut alongside Canopy so .wal files work out of the box" ON)
option(CANOPY_BUILD_TESTS "Build the Canopy tests" ON)
set(CANOPY_GROVE_TAG "v0.1.2" CACHE STRING "Grove git tag to fetch")

if(NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()

set(GROVE_BUILD_RUNTIME ON CACHE BOOL "" FORCE)
set(GROVE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GROVE_WITH_WALNUT ${CANOPY_WITH_WALNUT} CACHE BOOL "" FORCE)

include(FetchContent)
set(CANOPY_FETCH_EXTRA "")
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.25)
    list(APPEND CANOPY_FETCH_EXTRA SYSTEM)
endif()

FetchContent_Declare(grove
    GIT_REPOSITORY https://github.com/ThatNewCoderJSCpp/Grove.git
    GIT_TAG        ${CANOPY_GROVE_TAG}
    GIT_SHALLOW    TRUE
    ${CANOPY_FETCH_EXTRA})
FetchContent_MakeAvailable(grove)

set(CANOPY_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.hpp.in" "${CANOPY_GENERATED_DIR}/canopy/version.hpp" @ONLY)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/Defaults/settings.bark" CANOPY_DEFAULT_SETTINGS)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/defaults.cpp.in" "${CANOPY_GENERATED_DIR}/defaults.cpp" @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Defaults/settings.bark")

function(canopy_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

add_library(canopy_core STATIC
    Source/settings.cpp
    Source/languages.cpp
    Source/toolchain.cpp
    Source/build.cpp
    Source/workspace.cpp
    "${CANOPY_GENERATED_DIR}/defaults.cpp")
add_library(canopy::core ALIAS canopy_core)
target_compile_features(canopy_core PUBLIC cxx_std_17)
target_include_directories(canopy_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/Include"
    "${CANOPY_GENERATED_DIR}")
target_link_libraries(canopy_core PUBLIC grove::core)
canopy_warnings(canopy_core)

add_executable(canopy
    Tools/canopy.cpp
    Source/shell.cpp)
target_link_libraries(canopy PRIVATE canopy_core grove::runtime)
target_compile_definitions(canopy PRIVATE "CANOPY_SOURCE_UI_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/UI\"")
canopy_warnings(canopy)

add_custom_target(canopy_tools)
if(TARGET grove)
    add_dependencies(canopy_tools grove)
endif()
if(CANOPY_WITH_WALNUT AND TARGET walnut)
    add_dependencies(canopy_tools walnut)
endif()
add_dependencies(canopy canopy_tools)

if(CANOPY_BUILD_TESTS)
    enable_testing()
    add_executable(canopy_tests Tests/canopy_tests.cpp)
    target_link_libraries(canopy_tests PRIVATE canopy_core)
    target_compile_definitions(canopy_tests PRIVATE "CANOPY_TESTS_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/Tests\"")
    canopy_warnings(canopy_tests)
    add_test(NAME canopy_tests COMMAND canopy_tests)
    add_test(NAME canopy_doctor COMMAND canopy doctor)
    add_test(NAME canopy_check_ui COMMAND grove check "${CMAKE_CURRENT_SOURCE_DIR}/UI/canopy.grove")
    add_test(NAME canopy_build_c COMMAND canopy build "${CMAKE_CURRENT_SOURCE_DIR}/Tests/hello/hello.c" -o "${CMAKE_CURRENT_BINARY_DIR}/test-out/hello_c")
    add_test(NAME canopy_build_cpp COMMAND canopy run "${CMAKE_CURRENT_SOURCE_DIR}/Tests/hello/hello.cpp" -o "${CMAKE_CURRENT_BINARY_DIR}/test-out/hello_cpp")
    set_tests_properties(canopy_build_cpp PROPERTIES PASS_REGULAR_EXPRESSION "Hello from C\\+\\+")
    add_test(NAME canopy_build_grove COMMAND canopy build "${CMAKE_CURRENT_SOURCE_DIR}/Tests/hello/hello.grove" -o "${CMAKE_CURRENT_BINARY_DIR}/test-out/hello_grove")
    if(CANOPY_WITH_WALNUT AND TARGET walnut)
        add_test(NAME canopy_build_walnut COMMAND canopy run "${CMAKE_CURRENT_SOURCE_DIR}/Tests/hello/hello.wal" -o "${CMAKE_CURRENT_BINARY_DIR}/test-out/hello_wal")
        set_tests_properties(canopy_build_walnut PROPERTIES PASS_REGULAR_EXPRESSION "Hello from Walnut")
    endif()
endif()

include(GNUInstallDirs)
install(TARGETS canopy RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(TARGET grove)
    install(TARGETS grove RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
if(CANOPY_WITH_WALNUT AND TARGET walnut)
    install(TARGETS walnut RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/UI/" DESTINATION ${CMAKE_INSTALL_DATADIR}/canopy/ui)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/Defaults/settings.bark" DESTINATION ${CMAKE_INSTALL_DATADIR}/canopy)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/README.md" DESTINATION ${CMAKE_INSTALL_DOCDIR})
