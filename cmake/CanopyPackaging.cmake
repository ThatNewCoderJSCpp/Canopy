if(MSVC)
    message(FATAL_ERROR "CANOPY_PACKAGE needs a GCC or Clang toolchain, like Grove and Walnut")
endif()

string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _canopy_cpu)
if(_canopy_cpu MATCHES "^(x86_64|amd64|x64)$")
    if(CMAKE_SIZEOF_VOID_P EQUAL 4)
        set(_canopy_arch i686)
    else()
        set(_canopy_arch x86_64)
    endif()
elseif(_canopy_cpu MATCHES "^(aarch64|arm64)$")
    set(_canopy_arch arm64)
elseif(_canopy_cpu MATCHES "^(i[3-6]86|x86)$")
    set(_canopy_arch i686)
else()
    set(_canopy_arch ${_canopy_cpu})
endif()

set(_canopy_static_targets ${CANOPY_EXECUTABLES})
if(TARGET grove)
    list(APPEND _canopy_static_targets grove)
endif()

if(MINGW)
    foreach(t IN LISTS _canopy_static_targets)
        target_link_options(${t} PRIVATE -static)
    endforeach()
    include(CheckCXXSourceCompiles)
    check_cxx_source_compiles("#include <cstdlib>\n#ifndef _UCRT\n#error msvcrt\n#endif\nint main() { return 0; }" CANOPY_MINGW_UCRT)
    if(_canopy_arch STREQUAL "arm64")
        set(_canopy_platform windows-clangarm64)
    elseif(CANOPY_MINGW_UCRT)
        set(_canopy_platform windows-ucrt64)
    else()
        set(_canopy_platform windows-mingw64)
    endif()
    set(_canopy_generators "ZIP;7Z;NSIS")
    set(_canopy_walnut_archive zip)
else()
    foreach(t IN LISTS _canopy_static_targets)
        target_link_options(${t} PRIVATE -static-libstdc++ -static-libgcc)
    endforeach()
    set(_canopy_platform linux-${_canopy_arch})
    set(_canopy_generators "TGZ;TXZ;ZIP;DEB;RPM")
    set(_canopy_walnut_archive tar.gz)
endif()
set(CANOPY_PACKAGE_PLATFORM ${_canopy_platform} CACHE STRING "Platform name used in package file names")

if(CANOPY_BUNDLE_WALNUT)
    set(_walnut_name "Walnut-${CANOPY_WALNUT_VERSION}-${CANOPY_PACKAGE_PLATFORM}")
    set(_walnut_file "${CMAKE_BINARY_DIR}/walnut-download/${_walnut_name}.${_canopy_walnut_archive}")
    set(_walnut_dir "${CMAKE_BINARY_DIR}/walnut-bundle")
    set(CANOPY_WALNUT_ARCHIVE "" CACHE FILEPATH "A Walnut release package to bundle instead of downloading one")
    if(CANOPY_WALNUT_ARCHIVE)
        set(_walnut_file "${CANOPY_WALNUT_ARCHIVE}")
    elseif(NOT EXISTS "${_walnut_file}")
        set(_walnut_url "https://github.com/ThatNewCoderJSCpp/Walnut/releases/download/v${CANOPY_WALNUT_VERSION}/${_walnut_name}.${_canopy_walnut_archive}")
        message(STATUS "Downloading ${_walnut_url}")
        file(DOWNLOAD "${_walnut_url}" "${_walnut_file}.part" STATUS _walnut_status TLS_VERIFY ON)
        list(GET _walnut_status 0 _walnut_code)
        if(NOT _walnut_code EQUAL 0)
            list(GET _walnut_status 1 _walnut_error)
            message(FATAL_ERROR "Couldn't download ${_walnut_url}: ${_walnut_error}. Pass -DCANOPY_WALNUT_ARCHIVE=/path/to/${_walnut_name}.${_canopy_walnut_archive} or -DCANOPY_BUNDLE_WALNUT=OFF.")
        endif()
        file(RENAME "${_walnut_file}.part" "${_walnut_file}")
    endif()
    file(REMOVE_RECURSE "${_walnut_dir}")
    file(ARCHIVE_EXTRACT INPUT "${_walnut_file}" DESTINATION "${_walnut_dir}")
    file(GLOB _walnut_root LIST_DIRECTORIES true "${_walnut_dir}/Walnut-*")
    if(NOT _walnut_root)
        set(_walnut_root "${_walnut_dir}")
    endif()
    install(DIRECTORY "${_walnut_root}/bin/" DESTINATION bin USE_SOURCE_PERMISSIONS COMPONENT canopy)
    foreach(part runtime fizmo)
        if(EXISTS "${_walnut_root}/${part}")
            install(DIRECTORY "${_walnut_root}/${part}/" DESTINATION ${part} USE_SOURCE_PERMISSIONS COMPONENT canopy)
        endif()
    endforeach()
    if(EXISTS "${_walnut_root}/docs")
        install(DIRECTORY "${_walnut_root}/docs/" DESTINATION docs/walnut COMPONENT canopy)
    endif()
endif()

set(CPACK_PACKAGE_NAME Canopy)
set(CPACK_PACKAGE_VENDOR ThatNewCoderJSCpp)
set(CPACK_PACKAGE_CONTACT "ThatNewCoderJSCpp <https://github.com/ThatNewCoderJSCpp/Canopy>")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/ThatNewCoderJSCpp/Canopy")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Canopy: a code editor for C, C++, Walnut and Grove")
set(CPACK_PACKAGE_DESCRIPTION "Canopy is a code editor built with Grove. It edits, builds and runs C, C++, Walnut and Grove out of the box, finds your compilers for you, and keeps its settings in one small Bark file.")
set(CPACK_PACKAGE_FILE_NAME Canopy-${PROJECT_VERSION}-${CANOPY_PACKAGE_PLATFORM})
set(CPACK_GENERATOR ${_canopy_generators} CACHE STRING "Package formats cpack produces")
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
set(CPACK_STRIP_FILES ON)
set(CPACK_THREADS 0)
set(CPACK_PACKAGE_EXECUTABLES "canopy-gui;Canopy")
set(CPACK_INSTALL_CMAKE_PROJECTS "${CMAKE_BINARY_DIR};Canopy;canopy;/")
set(CPACK_PROJECT_CONFIG_FILE "${CMAKE_CURRENT_LIST_DIR}/CanopyCPackOptions.cmake")

set(_canopy_scripts "${CMAKE_BINARY_DIR}/package-scripts")
file(MAKE_DIRECTORY "${_canopy_scripts}/deb")
foreach(script postinst prerm)
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/Packaging/linux/deb-${script}.in" "${_canopy_scripts}/tmp/${script}" @ONLY)
    file(COPY "${_canopy_scripts}/tmp/${script}" DESTINATION "${_canopy_scripts}/deb"
        FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
endforeach()
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/Packaging/linux/rpm-post.sh.in" "${_canopy_scripts}/rpm-post.sh" @ONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/Packaging/linux/rpm-preun.sh.in" "${_canopy_scripts}/rpm-preun.sh" @ONLY)

set(CPACK_DEBIAN_PACKAGE_NAME canopy)
set(CPACK_DEBIAN_FILE_NAME ${CPACK_PACKAGE_FILE_NAME}.deb)
set(CPACK_DEBIAN_PACKAGE_SECTION devel)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "g++ | clang, gcc | clang")
set(CPACK_DEBIAN_PACKAGE_SUGGESTS "zenity | kdialog")
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA "${_canopy_scripts}/deb/postinst;${_canopy_scripts}/deb/prerm")

set(CPACK_RPM_PACKAGE_NAME canopy)
set(CPACK_RPM_FILE_NAME ${CPACK_PACKAGE_FILE_NAME}.rpm)
set(CPACK_RPM_PACKAGE_GROUP "Development/Tools")
set(CPACK_RPM_PACKAGE_LICENSE "See https://github.com/ThatNewCoderJSCpp/Canopy")
set(CPACK_RPM_PACKAGE_AUTOREQ ON)
set(CPACK_RPM_PACKAGE_AUTOPROV OFF)
set(CPACK_RPM_PACKAGE_SUGGESTS "gcc-c++, zenity")
set(CPACK_RPM_SPEC_MORE_DEFINE "%define _build_id_links none")
set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION /opt)
set(CPACK_RPM_POST_INSTALL_SCRIPT_FILE "${_canopy_scripts}/rpm-post.sh")
set(CPACK_RPM_PRE_UNINSTALL_SCRIPT_FILE "${_canopy_scripts}/rpm-preun.sh")
if(_canopy_arch STREQUAL "arm64")
    set(CPACK_RPM_PACKAGE_ARCHITECTURE aarch64)
    set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE arm64)
elseif(_canopy_arch STREQUAL "i686")
    set(CPACK_RPM_PACKAGE_ARCHITECTURE i686)
    set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE i386)
endif()

set(CPACK_NSIS_PACKAGE_NAME "Canopy ${PROJECT_VERSION}")
set(CPACK_NSIS_DISPLAY_NAME "Canopy")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Canopy")
set(CPACK_PACKAGE_INSTALL_REGISTRY_KEY "Canopy")
set(CPACK_NSIS_URL_INFO_ABOUT "https://github.com/ThatNewCoderJSCpp/Canopy")
set(CPACK_NSIS_MODIFY_PATH ON)
set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
set(CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES64")
set(CPACK_NSIS_MUI_ICON "${CMAKE_CURRENT_SOURCE_DIR}/Packaging/windows/canopy.ico")
set(CPACK_NSIS_MUI_UNIICON "${CMAKE_CURRENT_SOURCE_DIR}/Packaging/windows/canopy.ico")
set(CPACK_NSIS_INSTALLED_ICON_NAME "bin\\\\canopy-gui.exe")
set(CPACK_CREATE_DESKTOP_LINKS canopy-gui)
set(CPACK_NSIS_MUI_FINISHPAGE_RUN "canopy-gui.exe")
set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS "
    WriteRegStr SHCTX 'Software\\\\Classes\\\\Directory\\\\shell\\\\Canopy' '' 'Open with Canopy'
    WriteRegStr SHCTX 'Software\\\\Classes\\\\Directory\\\\shell\\\\Canopy' 'Icon' '$INSTDIR\\\\bin\\\\canopy-gui.exe'
    WriteRegStr SHCTX 'Software\\\\Classes\\\\Directory\\\\shell\\\\Canopy\\\\command' '' '\\\"$INSTDIR\\\\bin\\\\canopy-gui.exe\\\" --wait \\\"%V\\\"'
    WriteRegStr SHCTX 'Software\\\\Classes\\\\Directory\\\\Background\\\\shell\\\\Canopy' '' 'Open with Canopy'
    WriteRegStr SHCTX 'Software\\\\Classes\\\\Directory\\\\Background\\\\shell\\\\Canopy' 'Icon' '$INSTDIR\\\\bin\\\\canopy-gui.exe'
    WriteRegStr SHCTX 'Software\\\\Classes\\\\Directory\\\\Background\\\\shell\\\\Canopy\\\\command' '' '\\\"$INSTDIR\\\\bin\\\\canopy-gui.exe\\\" --wait \\\"%V\\\"'
    WriteRegStr SHCTX 'Software\\\\Classes\\\\*\\\\shell\\\\Canopy' '' 'Open with Canopy'
    WriteRegStr SHCTX 'Software\\\\Classes\\\\*\\\\shell\\\\Canopy' 'Icon' '$INSTDIR\\\\bin\\\\canopy-gui.exe'
    WriteRegStr SHCTX 'Software\\\\Classes\\\\*\\\\shell\\\\Canopy\\\\command' '' '\\\"$INSTDIR\\\\bin\\\\canopy-gui.exe\\\" --wait \\\"%1\\\"'
    WriteRegStr SHCTX 'Software\\\\Microsoft\\\\Windows\\\\CurrentVersion\\\\App Paths\\\\canopy.exe' '' '$INSTDIR\\\\bin\\\\canopy.exe'
    WriteRegStr SHCTX 'Software\\\\Microsoft\\\\Windows\\\\CurrentVersion\\\\App Paths\\\\canopy.exe' 'Path' '$INSTDIR\\\\bin'
    WriteRegStr SHCTX 'Software\\\\Classes\\\\Canopy.Walnut' '' 'Walnut source file'
    WriteRegStr SHCTX 'Software\\\\Classes\\\\Canopy.Walnut\\\\DefaultIcon' '' '$INSTDIR\\\\share\\\\canopy\\\\icons\\\\filetypes\\\\text-x-walnut.ico'
    WriteRegStr SHCTX 'Software\\\\Classes\\\\Canopy.Walnut\\\\shell\\\\open\\\\command' '' '\\\"$INSTDIR\\\\bin\\\\canopy-gui.exe\\\" --wait \\\"%1\\\"'
    WriteRegStr SHCTX 'Software\\\\Classes\\\\.wal\\\\OpenWithProgids' 'Canopy.Walnut' ''
    ReadRegStr $0 SHCTX 'Software\\\\Classes\\\\.wal' ''
    StrCmp $0 '' 0 +2
    WriteRegStr SHCTX 'Software\\\\Classes\\\\.wal' '' 'Canopy.Walnut'
    WriteRegStr SHCTX 'Software\\\\Classes\\\\Canopy.WalnutHeader' '' 'Walnut header'
    WriteRegStr SHCTX 'Software\\\\Classes\\\\Canopy.WalnutHeader\\\\DefaultIcon' '' '$INSTDIR\\\\share\\\\canopy\\\\icons\\\\filetypes\\\\text-x-walnut-header.ico'
    WriteRegStr SHCTX 'Software\\\\Classes\\\\Canopy.WalnutHeader\\\\shell\\\\open\\\\command' '' '\\\"$INSTDIR\\\\bin\\\\canopy-gui.exe\\\" --wait \\\"%1\\\"'
    WriteRegStr SHCTX 'Software\\\\Classes\\\\.walh\\\\OpenWithProgids' 'Canopy.WalnutHeader' ''
    ReadRegStr $0 SHCTX 'Software\\\\Classes\\\\.walh' ''
    StrCmp $0 '' 0 +2
    WriteRegStr SHCTX 'Software\\\\Classes\\\\.walh' '' 'Canopy.WalnutHeader'
    WriteRegStr SHCTX 'Software\\\\Classes\\\\.hwal\\\\OpenWithProgids' 'Canopy.WalnutHeader' ''
    ReadRegStr $0 SHCTX 'Software\\\\Classes\\\\.hwal' ''
    StrCmp $0 '' 0 +2
    WriteRegStr SHCTX 'Software\\\\Classes\\\\.hwal' '' 'Canopy.WalnutHeader'
    WriteRegStr SHCTX 'Software\\\\Classes\\\\Canopy.Grove' '' 'Grove document'
    WriteRegStr SHCTX 'Software\\\\Classes\\\\Canopy.Grove\\\\DefaultIcon' '' '$INSTDIR\\\\share\\\\canopy\\\\icons\\\\filetypes\\\\text-x-grove.ico'
    WriteRegStr SHCTX 'Software\\\\Classes\\\\Canopy.Grove\\\\shell\\\\open\\\\command' '' '\\\"$INSTDIR\\\\bin\\\\canopy-gui.exe\\\" --wait \\\"%1\\\"'
    WriteRegStr SHCTX 'Software\\\\Classes\\\\.grove\\\\OpenWithProgids' 'Canopy.Grove' ''
    ReadRegStr $0 SHCTX 'Software\\\\Classes\\\\.grove' ''
    StrCmp $0 '' 0 +2
    WriteRegStr SHCTX 'Software\\\\Classes\\\\.grove' '' 'Canopy.Grove'
    WriteRegStr SHCTX 'Software\\\\Classes\\\\Canopy.Bark' '' 'Bark stylesheet'
    WriteRegStr SHCTX 'Software\\\\Classes\\\\Canopy.Bark\\\\DefaultIcon' '' '$INSTDIR\\\\share\\\\canopy\\\\icons\\\\filetypes\\\\text-x-bark.ico'
    WriteRegStr SHCTX 'Software\\\\Classes\\\\Canopy.Bark\\\\shell\\\\open\\\\command' '' '\\\"$INSTDIR\\\\bin\\\\canopy-gui.exe\\\" --wait \\\"%1\\\"'
    WriteRegStr SHCTX 'Software\\\\Classes\\\\.bark\\\\OpenWithProgids' 'Canopy.Bark' ''
    ReadRegStr $0 SHCTX 'Software\\\\Classes\\\\.bark' ''
    StrCmp $0 '' 0 +2
    WriteRegStr SHCTX 'Software\\\\Classes\\\\.bark' '' 'Canopy.Bark'
    System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'
")
set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS "
    DeleteRegKey SHCTX 'Software\\\\Classes\\\\Directory\\\\shell\\\\Canopy'
    DeleteRegKey SHCTX 'Software\\\\Classes\\\\Directory\\\\Background\\\\shell\\\\Canopy'
    DeleteRegKey SHCTX 'Software\\\\Classes\\\\*\\\\shell\\\\Canopy'
    DeleteRegKey SHCTX 'Software\\\\Microsoft\\\\Windows\\\\CurrentVersion\\\\App Paths\\\\canopy.exe'
    DeleteRegKey SHCTX 'Software\\\\Classes\\\\Canopy.Walnut'
    DeleteRegValue SHCTX 'Software\\\\Classes\\\\.wal\\\\OpenWithProgids' 'Canopy.Walnut'
    ReadRegStr $0 SHCTX 'Software\\\\Classes\\\\.wal' ''
    StrCmp $0 'Canopy.Walnut' 0 +2
    DeleteRegValue SHCTX 'Software\\\\Classes\\\\.wal' ''
    DeleteRegKey SHCTX 'Software\\\\Classes\\\\Canopy.WalnutHeader'
    DeleteRegValue SHCTX 'Software\\\\Classes\\\\.walh\\\\OpenWithProgids' 'Canopy.WalnutHeader'
    ReadRegStr $0 SHCTX 'Software\\\\Classes\\\\.walh' ''
    StrCmp $0 'Canopy.WalnutHeader' 0 +2
    DeleteRegValue SHCTX 'Software\\\\Classes\\\\.walh' ''
    DeleteRegValue SHCTX 'Software\\\\Classes\\\\.hwal\\\\OpenWithProgids' 'Canopy.WalnutHeader'
    ReadRegStr $0 SHCTX 'Software\\\\Classes\\\\.hwal' ''
    StrCmp $0 'Canopy.WalnutHeader' 0 +2
    DeleteRegValue SHCTX 'Software\\\\Classes\\\\.hwal' ''
    DeleteRegKey SHCTX 'Software\\\\Classes\\\\Canopy.Grove'
    DeleteRegValue SHCTX 'Software\\\\Classes\\\\.grove\\\\OpenWithProgids' 'Canopy.Grove'
    ReadRegStr $0 SHCTX 'Software\\\\Classes\\\\.grove' ''
    StrCmp $0 'Canopy.Grove' 0 +2
    DeleteRegValue SHCTX 'Software\\\\Classes\\\\.grove' ''
    DeleteRegKey SHCTX 'Software\\\\Classes\\\\Canopy.Bark'
    DeleteRegValue SHCTX 'Software\\\\Classes\\\\.bark\\\\OpenWithProgids' 'Canopy.Bark'
    ReadRegStr $0 SHCTX 'Software\\\\Classes\\\\.bark' ''
    StrCmp $0 'Canopy.Bark' 0 +2
    DeleteRegValue SHCTX 'Software\\\\Classes\\\\.bark' ''
    System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'
")

include(CPack)
