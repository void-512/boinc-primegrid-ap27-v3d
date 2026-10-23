# BOINC has no top-level CMake build. Build only the Linux application API and
# its support code, without running its client/server/graphics/test builds.
set(BOINC_SOURCE_DIR "${PROJECT_SOURCE_DIR}/boinc")
if(NOT EXISTS "${BOINC_SOURCE_DIR}/api/boinc_api.cpp")
    message(FATAL_ERROR "BOINC submodule is missing. Run: git submodule update --init boinc")
endif()
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "The bundled BOINC application API build currently supports Linux only")
endif()

find_package(Threads REQUIRED)
include(CheckIncludeFileCXX)
include(CheckCXXSymbolExists)
include(CheckCXXSourceCompiles)

# These are the configure features used by the selected BOINC source files.
# Keep config.h in the build tree; the submodule is never modified.
foreach(header IN ITEMS dlfcn.h execinfo.h signal.h unistd.h sys/ipc.h
        sys/shm.h sys/resource.h sys/param.h sys/mount.h sys/statvfs.h)
    string(TOUPPER "${header}" feature)
    string(REGEX REPLACE "[/.]" "_" feature "${feature}")
    check_include_file_cxx("${header}" "HAVE_${feature}")
endforeach()
check_cxx_symbol_exists(sigaction "signal.h" HAVE_SIGACTION)
check_cxx_symbol_exists(ftok "sys/types.h;sys/ipc.h" HAVE_FTOK)
check_cxx_symbol_exists(strlcpy "string.h" HAVE_STRLCPY)
check_cxx_symbol_exists(strlcat "string.h" HAVE_STRLCAT)
# glibc overloads strcasestr in C++, so taking its address is ambiguous.
check_cxx_source_compiles("#include <string.h>\nint main() { return strcasestr(\"abc\", \"b\") == nullptr; }"
    BOINC_HAVE_STRCASESTR)
set(HAVE_STRCASESTR "${BOINC_HAVE_STRCASESTR}")
set(BOINC_CONFIG_DIR "${CMAKE_CURRENT_BINARY_DIR}/boinc-config")
configure_file("${CMAKE_CURRENT_LIST_DIR}/boinc_config.h.in"
    "${BOINC_CONFIG_DIR}/config.h")

# This is the dependency subset of lib/Makefile.am needed by boinc_api.cpp,
# including the host information and preferences read from init_data.xml.
set(boinc_core_sources
    app_ipc base64 coproc diagnostics filesys hostinfo
    md5 md5_file mem_usage mfile miofile opencl_boinc parse prefs
    proc_control procinfo procinfo_unix proxy_info shmem str_util url util)
list(TRANSFORM boinc_core_sources PREPEND "${BOINC_SOURCE_DIR}/lib/")
list(TRANSFORM boinc_core_sources APPEND ".cpp")
add_library(ap27_boinc_core STATIC ${boinc_core_sources})
add_library(BOINC::core ALIAS ap27_boinc_core)
add_library(ap27_boinc_api STATIC "${BOINC_SOURCE_DIR}/api/boinc_api.cpp")
add_library(BOINC::api ALIAS ap27_boinc_api)
foreach(target IN ITEMS ap27_boinc_core ap27_boinc_api)
    target_compile_features(${target} PRIVATE cxx_std_17)
    target_include_directories(${target}
        PRIVATE "${BOINC_CONFIG_DIR}" "${BOINC_SOURCE_DIR}"
        PUBLIC "${BOINC_SOURCE_DIR}/api" "${BOINC_SOURCE_DIR}/lib")
endforeach()
target_link_libraries(ap27_boinc_core PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
target_link_libraries(ap27_boinc_api PUBLIC BOINC::core)
