cmake_minimum_required(VERSION 3.18)
project(canopy VERSION 0.2.0 DESCRIPTION "Canopy: a code editor built with Grove" LANGUAGES C CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(CANOPY_WITH_WALNUT "Build Walnut alongside Canopy so .wal files work out of the box" ON)
option(CANOPY_BUILD_TESTS "Build the Canopy tests" ON)
option(CANOPY_PACKAGE "Build Canopy as a self-contained download and set up CPack" OFF)
option(CANOPY_BUNDLE_WALNUT "With CANOPY_PACKAGE: put Walnut's release package for this platform inside Canopy's" ON)
option(CANOPY_BUNDLE_COMPILER "With CANOPY_PACKAGE on Windows: bundle Walnut's -with-compiler package, so C, C++ and Walnut work offline, and add -with-compiler to the package names" OFF)
set(CANOPY_GROVE_TAG "v0.2.4" CACHE STRING "Grove git tag to fetch")
set(CANOPY_WALNUT_VERSION "0.2.5" CACHE STRING "Walnut release bundled into packages")

if(ANDROID)
    set(CANOPY_WITH_WALNUT OFF CACHE BOOL "" FORCE)
    set(CANOPY_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(CANOPY_PACKAGE OFF CACHE BOOL "" FORCE)
endif()

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

set(CANOPY_SPECIAL_NAMES_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/Source/special_names.cpp")
if(TARGET walnut_special_names_gen AND DEFINED WALNUT_SPECIAL_NAMES_JSON AND EXISTS "${WALNUT_SPECIAL_NAMES_JSON}")
    set(CANOPY_SPECIAL_NAMES_SOURCE "${CANOPY_GENERATED_DIR}/special_names.cpp")
    file(MAKE_DIRECTORY "${CANOPY_GENERATED_DIR}")
    add_custom_command(
        OUTPUT "${CANOPY_SPECIAL_NAMES_SOURCE}"
        COMMAND walnut_special_names_gen canopy "${WALNUT_SPECIAL_NAMES_JSON}" "${CANOPY_SPECIAL_NAMES_SOURCE}"
        DEPENDS walnut_special_names_gen "${WALNUT_SPECIAL_NAMES_JSON}"
        COMMENT "Generating Canopy's \\s{...} character names from Walnut's special_names.json"
        VERBATIM)
    message(STATUS "Canopy: \\s{...} names come from ${WALNUT_SPECIAL_NAMES_JSON}")
else()
    message(STATUS "Canopy: this Walnut has no special_names.json, so \\s{...} names come from Source/special_names.cpp")
endif()
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

set(CANOPY_JQUERY_FILE "${CMAKE_CURRENT_SOURCE_DIR}/Assets/jquery/jquery.min.js")
file(READ "${CANOPY_JQUERY_FILE}" CANOPY_JQUERY_HEX HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," CANOPY_JQUERY_BYTES "${CANOPY_JQUERY_HEX}")
file(STRINGS "${CANOPY_JQUERY_FILE}" CANOPY_JQUERY_HEAD LIMIT_COUNT 1)
string(REGEX MATCH "v([0-9]+\\.[0-9]+\\.[0-9]+)" _canopy_jquery_match "${CANOPY_JQUERY_HEAD}")
set(CANOPY_JQUERY_VERSION "${CMAKE_MATCH_1}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CANOPY_JQUERY_FILE}")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/jquery.cpp.in" "${CANOPY_GENERATED_DIR}/jquery.cpp" @ONLY)

set(CANOPY_WEB_DOCS_FILE "${CMAKE_CURRENT_SOURCE_DIR}/Assets/web/web_docs.txt")
file(READ "${CANOPY_WEB_DOCS_FILE}" CANOPY_WEB_DOCS_HEX HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," CANOPY_WEB_DOCS_BYTES "${CANOPY_WEB_DOCS_HEX}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CANOPY_WEB_DOCS_FILE}")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/web_docs.cpp.in" "${CANOPY_GENERATED_DIR}/web_docs_data.cpp" @ONLY)

file(GLOB CANOPY_LANGUAGE_DOC_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Assets/languages/*.txt")
list(SORT CANOPY_LANGUAGE_DOC_FILES)
set(CANOPY_LANGUAGE_DOCS_BYTES "")
foreach(_canopy_doc ${CANOPY_LANGUAGE_DOC_FILES})
    file(READ "${_canopy_doc}" _canopy_doc_hex HEX)
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," _canopy_doc_bytes "${_canopy_doc_hex}")
    string(APPEND CANOPY_LANGUAGE_DOCS_BYTES "${_canopy_doc_bytes}0x0a,")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_canopy_doc}")
endforeach()
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/language_docs.cpp.in" "${CANOPY_GENERATED_DIR}/language_docs_data.cpp" @ONLY)

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
    Source/run_stats.cpp
    Source/asm_info.cpp
    Source/workspace.cpp
    Source/shortcuts.cpp
    Source/desktop.cpp
    Source/terminal.cpp
    Source/markdown.cpp
    Source/live_server.cpp
    Source/github_auth.cpp
    Source/secure_store.cpp
    ${CANOPY_SPECIAL_NAMES_SOURCE}
    Source/project_names.cpp
    Source/git.cpp
    Source/history.cpp
    Source/tasks.cpp
    "${CANOPY_GENERATED_DIR}/defaults.cpp"
    Source/web_docs.cpp
    "${CANOPY_GENERATED_DIR}/jquery.cpp"
    "${CANOPY_GENERATED_DIR}/web_docs_data.cpp"
    "${CANOPY_GENERATED_DIR}/language_docs_data.cpp")
add_library(canopy::core ALIAS canopy_core)
set(CANOPY_GITHUB_CLIENT_ID "" CACHE STRING "Client ID of the GitHub OAuth app that Sign in to GitHub uses (empty: sign in with a pasted token)")
set_source_files_properties(Source/github_auth.cpp PROPERTIES COMPILE_DEFINITIONS "CANOPY_GITHUB_CLIENT_ID=\"${CANOPY_GITHUB_CLIENT_ID}\"")
target_compile_features(canopy_core PUBLIC cxx_std_17)
target_include_directories(canopy_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/Include"
    "${CANOPY_GENERATED_DIR}")
target_link_libraries(canopy_core PUBLIC grove::core)
if(TARGET fizmo::net)
    target_link_libraries(canopy_core PUBLIC fizmo::net)
else()
    target_link_libraries(canopy_core PUBLIC fizmo::fizmo)
endif()
if(WIN32)
    target_link_libraries(canopy_core PUBLIC ole32 uuid shell32 advapi32 user32)
endif()
if(APPLE)
    target_link_libraries(canopy_core PUBLIC "-framework Security" "-framework CoreFoundation")
endif()
if(NOT CANOPY_PACKAGE)
    target_compile_definitions(canopy_core PRIVATE "CANOPY_SOURCE_PACKAGING_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/Packaging\"")
endif()
canopy_warnings(canopy_core)

add_library(canopy_app STATIC
    Source/shell.cpp
    Source/settings_page.cpp
    Source/developer.cpp
    Source/git_panel.cpp
    Source/console_panel.cpp
    Source/elements_panel.cpp
    Source/find_bar.cpp
    Source/walnut_semantic.cpp
    Source/viewers.cpp
    Source/code_intel.cpp
    Source/literal_hover.cpp
    Source/web_intel.cpp
    Source/lang_intel.cpp
    Source/completion.cpp
    Source/language_docs.cpp
    Source/cli.cpp
    Source/askpass.cpp)
add_library(canopy::app ALIAS canopy_app)
target_link_libraries(canopy_app PUBLIC canopy_core grove::runtime)
if(NOT CANOPY_PACKAGE)
    target_compile_definitions(canopy_app PRIVATE "CANOPY_SOURCE_UI_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/UI\"")
endif()
canopy_warnings(canopy_app)

if(ANDROID)
    set(CANOPY_ANDROID_LIBS_DIR "${CMAKE_BINARY_DIR}/android-libs" CACHE PATH "Where the Android build puts lib/<abi>/libcanopy_android.so for grove build --runtime-lib")
    add_library(canopy_android SHARED Tools/canopy_android.cpp)
    target_link_libraries(canopy_android PRIVATE canopy_app)
    target_link_options(canopy_android PRIVATE
        "LINKER:-z,max-page-size=16384"
        "LINKER:-u,ANativeActivity_onCreate"
        "LINKER:--gc-sections"
        "$<$<NOT:$<CONFIG:Debug>>:LINKER:--strip-debug>"
        "LINKER:--version-script=${grove_SOURCE_DIR}/cmake/android_exports.map")
    set_property(TARGET canopy_android APPEND PROPERTY LINK_DEPENDS "${grove_SOURCE_DIR}/cmake/android_exports.map")
    set_target_properties(canopy_android PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CANOPY_ANDROID_LIBS_DIR}/${ANDROID_ABI}")
    canopy_warnings(canopy_android)
    return()
endif()

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
