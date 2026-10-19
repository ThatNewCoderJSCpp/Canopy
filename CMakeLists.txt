cmake_minimum_required(VERSION 3.18)
project(canopy VERSION 0.1.3 DESCRIPTION "Canopy: a code editor built with Grove" LANGUAGES C CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(CANOPY_WITH_WALNUT "Build Walnut alongside Canopy so .wal files work out of the box" ON)
option(CANOPY_BUILD_TESTS "Build the Canopy tests" ON)
option(CANOPY_PACKAGE "Build Canopy as a self-contained download and set up CPack" OFF)
option(CANOPY_BUNDLE_WALNUT "With CANOPY_PACKAGE: put Walnut's release package for this platform inside Canopy's" ON)
set(CANOPY_GROVE_TAG "v0.1.17" CACHE STRING "Grove git tag to fetch")
set(CANOPY_WALNUT_VERSION "0.1.15" CACHE STRING "Walnut release bundled into packages")

if(CANOPY_PACKAGE AND CANOPY_BUNDLE_WALNUT)
    set(CANOPY_WITH_WALNUT OFF CACHE BOOL "" FORCE)
endif()
if(WIN32)
    enable_language(RC)
endif()

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
file(GLOB CANOPY_CONFIG_FILES CONFIGURE_DEPENDS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/Config"
    "${CMAKE_CURRENT_SOURCE_DIR}/Config/*.bark"
    "${CMAKE_CURRENT_SOURCE_DIR}/Config/themes/*.bark")
list(SORT CANOPY_CONFIG_FILES)
set(CANOPY_DEFAULT_FILES "")
foreach(config_file IN LISTS CANOPY_CONFIG_FILES)
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/Config/${config_file}" config_text)
    string(APPEND CANOPY_DEFAULT_FILES "        { \"${config_file}\", R\"canopy_config(${config_text})canopy_config\" },\n")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Config/${config_file}")
endforeach()
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/defaults.cpp.in" "${CANOPY_GENERATED_DIR}/defaults.cpp" @ONLY)

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
    Source/shortcuts.cpp
    Source/desktop.cpp
    "${CANOPY_GENERATED_DIR}/defaults.cpp")
add_library(canopy::core ALIAS canopy_core)
target_compile_features(canopy_core PUBLIC cxx_std_17)
target_include_directories(canopy_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/Include"
    "${CANOPY_GENERATED_DIR}")
target_link_libraries(canopy_core PUBLIC grove::core)
if(WIN32)
    target_link_libraries(canopy_core PUBLIC ole32 uuid shell32 advapi32 user32)
endif()
if(NOT CANOPY_PACKAGE)
    target_compile_definitions(canopy_core PRIVATE "CANOPY_SOURCE_PACKAGING_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/Packaging\"")
endif()
canopy_warnings(canopy_core)

add_library(canopy_app STATIC
    Source/shell.cpp
    Source/settings_page.cpp
    Source/walnut_semantic.cpp
    Source/viewers.cpp
    Source/code_intel.cpp
    Source/language_docs.cpp
    Source/cli.cpp)
add_library(canopy::app ALIAS canopy_app)
target_link_libraries(canopy_app PUBLIC canopy_core grove::runtime)
if(NOT CANOPY_PACKAGE)
    target_compile_definitions(canopy_app PRIVATE "CANOPY_SOURCE_UI_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/UI\"")
endif()
canopy_warnings(canopy_app)

function(canopy_resources target name)
    if(WIN32)
        set(CANOPY_RC_NAME ${name})
        set(CANOPY_ICON_PATH "${CMAKE_CURRENT_SOURCE_DIR}/Packaging/windows/canopy.ico")
        configure_file("${CMAKE_CURRENT_SOURCE_DIR}/Packaging/windows/canopy.rc.in" "${CANOPY_GENERATED_DIR}/${name}.rc" @ONLY)
        target_sources(${target} PRIVATE "${CANOPY_GENERATED_DIR}/${name}.rc")
    endif()
endfunction()

add_executable(canopy Tools/canopy.cpp)
target_link_libraries(canopy PRIVATE canopy_app)
canopy_resources(canopy canopy)
canopy_warnings(canopy)

set(CANOPY_EXECUTABLES canopy)
if(WIN32)
    add_executable(canopy-gui WIN32 Tools/canopy_gui.cpp)
    target_link_libraries(canopy-gui PRIVATE canopy_app)
    canopy_resources(canopy-gui canopy-gui)
    canopy_warnings(canopy-gui)
    list(APPEND CANOPY_EXECUTABLES canopy-gui)
endif()

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
    target_link_libraries(canopy_tests PRIVATE canopy_app)
    target_compile_definitions(canopy_tests PRIVATE "CANOPY_TESTS_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/Tests\"")
    canopy_warnings(canopy_tests)
    add_test(NAME canopy_tests COMMAND canopy_tests)
    add_test(NAME canopy_doctor COMMAND canopy doctor)
    add_test(NAME canopy_install_user COMMAND canopy install --desktop)
    set_tests_properties(canopy_install_user PROPERTIES
        ENVIRONMENT "HOME=${CMAKE_CURRENT_BINARY_DIR}/test-home;XDG_DATA_HOME=${CMAKE_CURRENT_BINARY_DIR}/test-home/.local/share;XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/test-home/.config;APPDATA=${CMAKE_CURRENT_BINARY_DIR}/test-home"
        PASS_REGULAR_EXPRESSION "Done")
    add_test(NAME canopy_uninstall_user COMMAND canopy uninstall)
    set_tests_properties(canopy_uninstall_user PROPERTIES
        DEPENDS canopy_install_user
        ENVIRONMENT "HOME=${CMAKE_CURRENT_BINARY_DIR}/test-home;XDG_DATA_HOME=${CMAKE_CURRENT_BINARY_DIR}/test-home/.local/share;XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/test-home/.config;APPDATA=${CMAKE_CURRENT_BINARY_DIR}/test-home")
    add_test(NAME canopy_check_ui COMMAND grove check "${CMAKE_CURRENT_SOURCE_DIR}/UI/canopy.grove")
    add_test(NAME canopy_dump_ui COMMAND canopy dump "${CMAKE_CURRENT_SOURCE_DIR}/Tests")
    set_tests_properties(canopy_dump_ui PROPERTIES
        ENVIRONMENT "XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/test-config;APPDATA=${CMAKE_CURRENT_BINARY_DIR}/test-config;CANOPY_UI_DIR=${CMAKE_CURRENT_SOURCE_DIR}/UI"
        PASS_REGULAR_EXPRESSION "data-path=\"[^\"]*hello")
    add_test(NAME canopy_build_c COMMAND canopy build "${CMAKE_CURRENT_SOURCE_DIR}/Tests/hello/hello.c" -o "${CMAKE_CURRENT_BINARY_DIR}/test-out/hello_c")
    add_test(NAME canopy_build_cpp COMMAND canopy run "${CMAKE_CURRENT_SOURCE_DIR}/Tests/hello/hello.cpp" -o "${CMAKE_CURRENT_BINARY_DIR}/test-out/hello_cpp")
    set_tests_properties(canopy_build_cpp PROPERTIES PASS_REGULAR_EXPRESSION "Hello from C\\+\\+")
    add_test(NAME canopy_build_grove COMMAND canopy build "${CMAKE_CURRENT_SOURCE_DIR}/Tests/hello/hello.grove" -o "${CMAKE_CURRENT_BINARY_DIR}/test-out/hello_grove")
    if(CANOPY_WITH_WALNUT AND TARGET walnut)
        add_test(NAME canopy_build_walnut COMMAND canopy run "${CMAKE_CURRENT_SOURCE_DIR}/Tests/hello/hello.wal" -o "${CMAKE_CURRENT_BINARY_DIR}/test-out/hello_wal")
        set_tests_properties(canopy_build_walnut PROPERTIES PASS_REGULAR_EXPRESSION "Hello from Walnut")
    endif()
endif()

install(TARGETS ${CANOPY_EXECUTABLES} RUNTIME DESTINATION bin COMPONENT canopy)
if(TARGET grove)
    install(TARGETS grove RUNTIME DESTINATION bin COMPONENT canopy)
    install(DIRECTORY "${grove_SOURCE_DIR}/Include/" DESTINATION include COMPONENT canopy)
    install(FILES "${grove_BINARY_DIR}/generated/grove/version.hpp" DESTINATION include/grove COMPONENT canopy)
    install(DIRECTORY "${grove_SOURCE_DIR}/Documentation/" DESTINATION docs/grove COMPONENT canopy)
endif()
if(CANOPY_WITH_WALNUT AND TARGET walnut)
    install(TARGETS walnut RUNTIME DESTINATION bin COMPONENT canopy)
endif()
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/UI/" DESTINATION share/canopy/ui COMPONENT canopy)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/Config/" DESTINATION share/canopy/config COMPONENT canopy)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/Packaging/icons/" DESTINATION share/canopy/icons COMPONENT canopy)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/README.md" DESTINATION docs COMPONENT canopy)

if(CANOPY_PACKAGE)
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/CanopyPackaging.cmake")
endif()
