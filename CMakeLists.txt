cmake_minimum_required(VERSION 3.31)

set(BOINC_WORK_DIR "" CACHE PATH "Working directory of the BOINC client")
if(BOINC_WORK_DIR STREQUAL "")
    message(FATAL_ERROR
        "BOINC_WORK_DIR is required; configure with -DBOINC_WORK_DIR=<BOINC working directory>.\n"
        "Use: systemctl show -p WorkingDirectory --value boinc-client")
endif()

project(ap27_v3d LANGUAGES CXX)

find_package(Vulkan REQUIRED)
find_program(GLSLC_EXECUTABLE NAMES glslc REQUIRED)

include(cmake/BOINC.cmake)

option(AP27_STATIC_CXX_RUNTIME "Statically link the C++ and compiler runtimes when available" ON)

set(AP27_BIN_DIR "${CMAKE_BINARY_DIR}/bin")
set(AP27_SHADER_DIR "${PROJECT_SOURCE_DIR}/shaders")
set(AP27_SHADER_OUTPUTS)
foreach(stage RANGE 0 7)
    set(output "${AP27_BIN_DIR}/stage_${stage}.spv")
    add_custom_command(OUTPUT "${output}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${AP27_BIN_DIR}"
        COMMAND "${GLSLC_EXECUTABLE}" -O "-I${AP27_SHADER_DIR}"
            "-DSTAGE=${stage}" -fshader-stage=comp -o "${output}" "${AP27_SHADER_DIR}/tile.comp"
        DEPENDS "${AP27_SHADER_DIR}/tile.comp"
            "${AP27_SHADER_DIR}/sieve_masks_early.glsl" "${AP27_SHADER_DIR}/sieve_masks_middle.glsl" "${AP27_SHADER_DIR}/sieve_masks_late.glsl"
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
    CXX_EXTENSIONS NO RUNTIME_OUTPUT_DIRECTORY "${AP27_BIN_DIR}")
target_include_directories(ap27_v3d PRIVATE "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(ap27_v3d PRIVATE Vulkan::Vulkan BOINC::api)
if(AP27_STATIC_CXX_RUNTIME)
    include(CheckCXXSourceCompiles)
    include(CMakePushCheckState)
    cmake_push_check_state(RESET)
    set(CMAKE_REQUIRED_LINK_OPTIONS -static-libstdc++ -static-libgcc)
    check_cxx_source_compiles("#include <iostream>\nint main() { std::cout << 0; }"
        AP27_HAS_STATIC_CXX_RUNTIME)
    cmake_pop_check_state()
    if(AP27_HAS_STATIC_CXX_RUNTIME)
        target_link_options(ap27_v3d PRIVATE -static-libstdc++ -static-libgcc)
    else()
        message(WARNING "Static C++ runtime unavailable; using the shared runtime")
    endif()
endif()
message(STATUS "BOINC is linked statically; Vulkan and the system C runtime remain shared")
add_dependencies(ap27_v3d ap27_shaders)

# Generate the V3D statistics monitor outside the BOINC payload, beside build/bin.
add_executable(v3d_smi tools/v3d_smi.cpp)
target_compile_features(v3d_smi PRIVATE cxx_std_17)
set_target_properties(v3d_smi PROPERTIES
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# The default build assembles exactly the files used by the BOINC app version.
configure_file(app_info.xml "${AP27_BIN_DIR}/app_info.xml" COPYONLY)
set(AP27_APP_INFO "${AP27_BIN_DIR}/app_info.xml")
set(AP27_RUNTIME_FILES ${AP27_SHADER_OUTPUTS})
configure_file("${BOINC_SOURCE_DIR}/COPYING" "${AP27_BIN_DIR}/BOINC-COPYING.txt" COPYONLY)
configure_file("${BOINC_SOURCE_DIR}/COPYING.LESSER" "${AP27_BIN_DIR}/BOINC-COPYING.LESSER.txt" COPYONLY)
list(APPEND AP27_RUNTIME_FILES
    "${AP27_BIN_DIR}/BOINC-COPYING.txt" "${AP27_BIN_DIR}/BOINC-COPYING.LESSER.txt")
add_custom_target(ap27_payload ALL DEPENDS ap27_v3d v3d_smi ${AP27_RUNTIME_FILES} "${AP27_APP_INFO}")

set(AP27_INSTALL_DIR "${BOINC_WORK_DIR}/projects/www.primegrid.com")
install(TARGETS ap27_v3d RUNTIME DESTINATION "${AP27_INSTALL_DIR}")
install(FILES ${AP27_RUNTIME_FILES} DESTINATION "${AP27_INSTALL_DIR}")
install(FILES "${AP27_APP_INFO}" DESTINATION "${AP27_INSTALL_DIR}")
