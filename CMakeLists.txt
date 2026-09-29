cmake_minimum_required(VERSION 3.31)
project(ap27_v3d LANGUAGES CXX)

find_package(Vulkan REQUIRED)
find_program(GLSLC_EXECUTABLE NAMES glslc REQUIRED)

set(BOINC_ROOT "" CACHE PATH "BOINC development installation prefix")
if(NOT BOINC_ROOT AND EXISTS "${PROJECT_SOURCE_DIR}/.local/sources/boinc-packages/root/usr/include/boinc/boinc_api.h")
    set(BOINC_ROOT "${PROJECT_SOURCE_DIR}/.local/sources/boinc-packages/root/usr")
endif()
find_path(BOINC_INCLUDE_DIR NAMES boinc_api.h
    HINTS "${BOINC_ROOT}" PATH_SUFFIXES include/boinc include)
find_library(BOINC_API_LIBRARY NAMES boinc_api
    HINTS "${BOINC_ROOT}" PATH_SUFFIXES "lib/${CMAKE_LIBRARY_ARCHITECTURE}" lib64 lib)
find_library(BOINC_CORE_LIBRARY NAMES boinc
    HINTS "${BOINC_ROOT}" PATH_SUFFIXES "lib/${CMAKE_LIBRARY_ARCHITECTURE}" lib64 lib)
if(NOT BOINC_INCLUDE_DIR OR NOT BOINC_API_LIBRARY OR NOT BOINC_CORE_LIBRARY)
    message(FATAL_ERROR "BOINC headers and shared libraries are required; install libboinc-app-dev or set BOINC_ROOT")
endif()

get_filename_component(BOINC_API_REAL "${BOINC_API_LIBRARY}" REALPATH)
get_filename_component(BOINC_CORE_REAL "${BOINC_CORE_LIBRARY}" REALPATH)
get_filename_component(BOINC_API_FILE "${BOINC_API_REAL}" NAME)
get_filename_component(BOINC_CORE_FILE "${BOINC_CORE_REAL}" NAME)
string(REGEX MATCH "^libboinc_api\\.so\\.[0-9]+" BOINC_API_SONAME "${BOINC_API_FILE}")
string(REGEX MATCH "^libboinc\\.so\\.[0-9]+" BOINC_CORE_SONAME "${BOINC_CORE_FILE}")
if(NOT BOINC_API_SONAME OR NOT BOINC_CORE_SONAME)
    message(FATAL_ERROR "Shared BOINC libraries with versioned SONAMEs are required for BOINC packaging")
endif()

add_library(BOINC::api UNKNOWN IMPORTED)
set_target_properties(BOINC::api PROPERTIES IMPORTED_LOCATION "${BOINC_API_LIBRARY}")
add_library(BOINC::core UNKNOWN IMPORTED)
set_target_properties(BOINC::core PROPERTIES IMPORTED_LOCATION "${BOINC_CORE_LIBRARY}")

set(AP27_BIN_DIR "${CMAKE_BINARY_DIR}/bin")
set(AP27_SHADER_DIR "${PROJECT_SOURCE_DIR}/shaders")
set(AP27_SHADER_OUTPUTS)
foreach(stage RANGE 0 5)
    set(output "${AP27_BIN_DIR}/stage_${stage}.spv")
    add_custom_command(OUTPUT "${output}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${AP27_BIN_DIR}"
        COMMAND "${GLSLC_EXECUTABLE}" -O "-I${AP27_SHADER_DIR}"
            "-DSTAGE=${stage}" -fshader-stage=comp -o "${output}" "${AP27_SHADER_DIR}/tile.comp"
        DEPENDS "${AP27_SHADER_DIR}/tile.comp"
            "${AP27_SHADER_DIR}/sieve_masks_early.glsl" "${AP27_SHADER_DIR}/sieve_masks_late.glsl"
            "${AP27_SHADER_DIR}/ap27_math.glsl"
        VERBATIM)
    list(APPEND AP27_SHADER_OUTPUTS "${output}")
endforeach()
add_custom_target(ap27_shaders DEPENDS ${AP27_SHADER_OUTPUTS})

add_executable(ap27_v3d
    src/AP26_v3d.cpp
    src/v3d_search.cpp)
target_compile_features(ap27_v3d PRIVATE cxx_std_20)
set_target_properties(ap27_v3d PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO RUNTIME_OUTPUT_DIRECTORY "${AP27_BIN_DIR}"
    BUILD_WITH_INSTALL_RPATH YES INSTALL_RPATH "$ORIGIN")
target_include_directories(ap27_v3d PRIVATE "${PROJECT_SOURCE_DIR}/include" "${BOINC_INCLUDE_DIR}")
target_link_libraries(ap27_v3d PRIVATE Vulkan::Vulkan BOINC::api BOINC::core)
add_dependencies(ap27_v3d ap27_shaders)

# The default build assembles exactly the files used by the BOINC app version.
configure_file(boinc/app_info.xml.in "${AP27_BIN_DIR}/app_info.xml" @ONLY)
set(AP27_APP_INFO "${AP27_BIN_DIR}/app_info.xml")
set(AP27_RUNTIME_FILES ${AP27_SHADER_OUTPUTS})
foreach(boinc_library IN ITEMS "${BOINC_API_REAL}" "${BOINC_CORE_REAL}")
    get_filename_component(boinc_library_name "${boinc_library}" NAME)
    string(REGEX MATCH "^libboinc(_api)?\\.so\\.[0-9]+" boinc_soname "${boinc_library_name}")
    set(output "${AP27_BIN_DIR}/${boinc_soname}")
    add_custom_command(OUTPUT "${output}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${AP27_BIN_DIR}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${boinc_library}" "${output}"
        DEPENDS "${boinc_library}" VERBATIM)
    list(APPEND AP27_RUNTIME_FILES "${output}")
endforeach()
foreach(notice_path IN ITEMS
    "${BOINC_ROOT}/share/doc/libboinc-app7t64/copyright"
    "${BOINC_ROOT}/share/doc/libboinc-app-dev/copyright"
    "/usr/share/doc/libboinc-app7t64/copyright"
    "/usr/share/doc/libboinc-app-dev/copyright")
    if(EXISTS "${notice_path}")
        set(notice_output "${AP27_BIN_DIR}/BOINC-COPYRIGHT.txt")
        add_custom_command(OUTPUT "${notice_output}"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${notice_path}" "${notice_output}"
            DEPENDS "${notice_path}" VERBATIM)
        list(APPEND AP27_RUNTIME_FILES "${notice_output}")
        break()
    endif()
endforeach()
add_custom_target(ap27_payload ALL DEPENDS ap27_v3d ${AP27_RUNTIME_FILES} "${AP27_APP_INFO}")

set(AP27_INSTALL_DIR "/var/lib/boinc-client/projects/www.primegrid.com")
install(TARGETS ap27_v3d RUNTIME DESTINATION "${AP27_INSTALL_DIR}")
install(FILES ${AP27_RUNTIME_FILES} DESTINATION "${AP27_INSTALL_DIR}")
install(FILES "${AP27_APP_INFO}" DESTINATION "${AP27_INSTALL_DIR}")
