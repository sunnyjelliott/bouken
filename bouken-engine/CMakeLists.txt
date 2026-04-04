cmake_minimum_required(VERSION 3.31)
project(BoukenEngine)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_VERBOSE_MAKEFILE ON)

if(WIN32)
    if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        message(STATUS "Switching to MSVC for USD compatibility")
        set(CMAKE_C_COMPILER "cl" CACHE FILEPATH "C compiler" FORCE)
        set(CMAKE_CXX_COMPILER "cl" CACHE FILEPATH "C++ compiler" FORCE)
    endif()
endif()

include(FetchContent)

# --------------------------------
#            Vulkan
# --------------------------------
find_package(Vulkan REQUIRED)

# --------------------------------
#             GLFW
# --------------------------------
find_package(glfw3 3.4 QUIET)
if(NOT glfw3_FOUND)
    message(STATUS "GLFW not found via find_package, fetching from source...")
    
    FetchContent_Declare(
        glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG 3.4
        SYSTEM
    )
    
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
    
    FetchContent_MakeAvailable(glfw)
    
    add_library(glfw3::glfw3 ALIAS glfw)
else()
    message(STATUS "Found GLFW via find_package")
endif()

# --------------------------------
#               GLM
# --------------------------------
find_package(glm QUIET)
if(NOT glm_FOUND)
    message(STATUS "GLM not found via find_package, fetching from source...")
    
    FetchContent_Declare(
        glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG 1.0.3
        SYSTEM
    )
    
    FetchContent_MakeAvailable(glm)
else()
    message(STATUS "Found GLM via find_package")
endif()

# --------------------------------
#           tinyobjloader
# --------------------------------
FetchContent_Declare(
    tinyobjloader
    GIT_REPOSITORY https://github.com/tinyobjloader/tinyobjloader.git
    GIT_TAG v2.0.0rc13
    SYSTEM
)
FetchContent_MakeAvailable(tinyobjloader)

# --------------------------------
#               stb
# --------------------------------
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG master
    SYSTEM
)
FetchContent_MakeAvailable(stb)

# --------------------------------
#               vma
# --------------------------------
FetchContent_Declare(
    vma
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG v3.3.0
    SYSTEM
)
FetchContent_MakeAvailable(vma)

# --------------------------------
#           MikkTSpace
# --------------------------------
FetchContent_Declare(
    mikktspace
    GIT_REPOSITORY https://github.com/mmikk/MikkTSpace.git
    GIT_TAG        master
    SYSTEM
)
FetchContent_MakeAvailable(mikktspace)


# --------------------------------
#             OpenUSD
# --------------------------------

# Location selection (arg|env|vcpkg)
set(USD_ROOT "" CACHE PATH "Path to manual OpenUSD install")
if(NOT USD_ROOT AND DEFINED ENV{USD_ROOT})
    set(USD_ROOT $ENV{USD_ROOT})
endif()

if(USD_ROOT)

    # Build variant selection
    if(CMAKE_BUILD_TYPE MATCHES "Debug")
        set(USD_VARIANT_DIR "${USD_ROOT}/debug")
    else()
        set(USD_VARIANT_DIR "${USD_ROOT}/release")
    endif()
    # Detect what compiler USD was built with
    set(USD_CACHE_FILE "${USD_VARIANT_DIR}/build/OpenUSD/CMakeCache.txt")
    if(EXISTS ${USD_CACHE_FILE})
        file(STRINGS ${USD_CACHE_FILE} USD_COMPILER_LINE REGEX "CMAKE_CXX_COMPILER:FILEPATH=")
        if(USD_COMPILER_LINE)
            string(REGEX MATCH "=(.+)$" _ ${USD_COMPILER_LINE})
            set(USD_COMPILER ${CMAKE_MATCH_1})  
            get_filename_component(USD_COMPILER_NAME ${USD_COMPILER} NAME_WE)
            
            message(STATUS "USD was built with compiler: ${USD_COMPILER_NAME}")
            
            # Force matching compiler if different
            get_filename_component(CURRENT_COMPILER_NAME ${CMAKE_CXX_COMPILER} NAME_WE)
            if(NOT CURRENT_COMPILER_NAME STREQUAL USD_COMPILER_NAME)
                message(WARNING "Compiler mismatch detected!")
                message(WARNING "  USD built with: ${USD_COMPILER_NAME}")
                message(WARNING "  Current compiler: ${CURRENT_COMPILER_NAME}")
                message(STATUS "Switching to USD's compiler: ${USD_COMPILER}")
                
                set(CMAKE_C_COMPILER ${USD_COMPILER} CACHE FILEPATH "C compiler" FORCE)
                set(CMAKE_CXX_COMPILER ${USD_COMPILER} CACHE FILEPATH "C++ compiler" FORCE)
                
                message(FATAL_ERROR "Compiler changed. Please reconfigure from clean build directory.")
            endif()
        endif()
    endif()
    
    if(EXISTS "${USD_VARIANT_DIR}/lib/cmake")
        message(STATUS "Using USD ${CMAKE_BUILD_TYPE} variant at: ${USD_VARIANT_DIR}")
        list(PREPEND CMAKE_PREFIX_PATH "${USD_VARIANT_DIR}")
    else()
        message(FATAL_ERROR "USD variant directory invalid: ${USD_VARIANT_DIR}")
    endif()
else()
    message(STATUS "No manual USD specified. Falling back to default package resolution (vcpkg if toolchain is set).")
endif()

find_package(pxr CONFIG REQUIRED)

set(USD_LIBS
    arch
    tf
    gf
    vt
    work
    plug
    trace
    js
    ar
    sdf
    pcp
    usd
    kind
    usdGeom
    usdShade
    usdLux
)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

file(GLOB_RECURSE SOURCES src/*)
add_executable(${PROJECT_NAME} 
    ${SOURCES}
)

find_program(GLSLC glslc HINTS 
    $ENV{VULKAN_SDK}/Bin
    $ENV{VULKAN_SDK}/bin
)

if(NOT GLSLC)
    message(FATAL_ERROR "glslc not found! Make sure Vulkan SDK is installed.")
endif()

# Shader compilation
set(SHADER_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(SHADER_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)

file(MAKE_DIRECTORY ${SHADER_BINARY_DIR})

set(SHADERS
    ${SHADER_SOURCE_DIR}/shader.vert
    ${SHADER_SOURCE_DIR}/depth.vert
    ${SHADER_SOURCE_DIR}/fullscreen.vert
    ${SHADER_SOURCE_DIR}/geometry.vert

    ${SHADER_SOURCE_DIR}/shader.frag
    ${SHADER_SOURCE_DIR}/geometry.frag
    ${SHADER_SOURCE_DIR}/lighting.frag
    ${SHADER_SOURCE_DIR}/tonemap.frag
)

foreach(SHADER ${SHADERS})
    get_filename_component(SHADER_NAME ${SHADER} NAME)
    string(REPLACE "." "_" SHADER_NAME ${SHADER_NAME})
    set(SHADER_OUTPUT ${SHADER_BINARY_DIR}/${SHADER_NAME}.spv)
    
    add_custom_command(
        OUTPUT ${SHADER_OUTPUT}
        COMMAND ${GLSLC} ${SHADER} -o ${SHADER_OUTPUT}
        DEPENDS ${SHADER}
        COMMENT "Compiling ${SHADER}"
    )
    
    list(APPEND SHADER_BINARIES ${SHADER_OUTPUT})
endforeach()

add_custom_target(Shaders ALL DEPENDS ${SHADER_BINARIES})
add_dependencies(${PROJECT_NAME} Shaders)

# Copy assets folder to build directory
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets
        ${CMAKE_BINARY_DIR}/assets
    COMMENT "Copying assets to build directory"
)

if(USD_ROOT)
    file(GLOB USD_LIB_DLLS "${USD_VARIANT_DIR}/lib/*.dll")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${USD_VARIANT_DIR}/bin ${CMAKE_BINARY_DIR}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${USD_LIB_DLLS} ${CMAKE_BINARY_DIR}
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${USD_VARIANT_DIR}/lib/usd ${CMAKE_BINARY_DIR}/usd
        COMMENT "Copying USD DLLs and plugins (${CMAKE_BUILD_TYPE})"
    )
endif()

target_sources(BoukenEngine PRIVATE
    ${mikktspace_SOURCE_DIR}/mikktspace.c
)
target_include_directories(${PROJECT_NAME} PRIVATE 
    include
    include/render
    include/lighting
    ${stb_SOURCE_DIR}
    ${mikktspace_SOURCE_DIR}
)

target_link_libraries(${PROJECT_NAME} PRIVATE 
    Vulkan::Vulkan
    glfw
    glm::glm
    tinyobjloader
    VulkanMemoryAllocator
    ${USD_LIBS}
)

target_precompile_headers(BoukenEngine PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/include/pch.h>"
)
target_compile_definitions(${PROJECT_NAME} PRIVATE PXR_ENABLE_PYTHON=0)