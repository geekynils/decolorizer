cmake_minimum_required(VERSION 3.7)

project(Decolorizer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -Wall -Wextra")
if (CMAKE_SYSTEM_NAME STREQUAL Emscripten)
    set(CMAKE_EXECUTABLE_SUFFIX ".html")
endif()

set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -s")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -s")
set(CMAKE_CXX_EXTENSIONS OFF)

# sanitize w/o xcode
# set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address,undefined")
# set(CMAKE_LINKER_FLAGS_DEBUG "${CMAKE_LINKER_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address,undefined")

set(SOKOL_HEADERS CACHE FILEPATH "")
set(SOKOL_TOOLS_BIN CACHE FILEPATH "Path to the sokol tools binaries, must contain sokol-shdc.")
set(IMGUI_PATH CACHE FILEPATH "")
set(HIMAGE_PATH CACHE FILEPATH "")
set(STB_HEADERS CACHE FILEPATH "")

include_directories(${SOKOL_HEADERS} ${SOKOL_HEADERS}/util ${STB_HEADERS} ${IMGUI_PATH} ${HIMAGE_PATH})

add_library(himage STATIC 
    ${HIMAGE_PATH}/canny.h
    ${HIMAGE_PATH}/canny.cpp
    ${HIMAGE_PATH}/image.h
    ${HIMAGE_PATH}/image.cpp
    ${HIMAGE_PATH}/image_ops.h 
    ${HIMAGE_PATH}/image_ops.cpp
    ${HIMAGE_PATH}/vec.h
    ${HIMAGE_PATH}/vec.cpp
)

add_library(imgui STATIC 
    ${IMGUI_PATH}/imgui.cpp
    ${IMGUI_PATH}/imgui.h
    ${IMGUI_PATH}/imgui_widgets.cpp 
    ${IMGUI_PATH}/imgui_draw.cpp 
    ${IMGUI_PATH}/imgui_tables.cpp
    ${IMGUI_PATH}/imgui_demo.cpp)

# add headers to ide for reference
FILE(GLOB SOKOL_HEADER_LIST ${SOKOL_HEADERS}/*.h)
add_custom_target(sokol_headers SOURCES ${SOKOL_HEADER_LIST})

# shader compilation
# if(CMAKE_SYSTEM_NAME STREQUAL Darwin)
add_custom_command(
  OUTPUT shader.glsl.h
  COMMAND ${SOKOL_TOOLS_BIN}/sokol-shdc
          -i ${CMAKE_CURRENT_SOURCE_DIR}/shader.glsl 
          -o ${CMAKE_CURRENT_SOURCE_DIR}/shader.glsl.h 
          -l metal_macos:glsl100 # glsl100 is for WebGL 1
  VERBATIM
)

add_custom_target(shaders
    SOURCES shader.glsl
)

set(SRC
    main.cpp
)

set(HEADER
    shader.glsl.h
)

# build sokol and Mac specific functionality
if(CMAKE_SYSTEM_NAME STREQUAL Darwin)
    list(APPEND HEADER mac_utils.h)
    add_library(mac_utils STATIC mac_utils.m mac_utils.h)

    add_definitions(-DSOKOL_METAL)
    add_library(sokol STATIC sokol_mac_impl.m ${SOKOL_HEADERS} ${STB_HEADERS})
    target_link_libraries(sokol PUBLIC
        "-framework QuartzCore"
        "-framework Cocoa"
        "-framework MetalKit"
        "-framework Metal")
elseif(CMAKE_SYSTEM_NAME STREQUAL Emscripten)
    add_definitions(-DSOKOL_GLES2) # WebGL
    add_library(sokol STATIC sokol_impl.c ${SOKOL_HEADERS} ${STB_HEADERS})
endif()

add_executable(decolorizer ${SRC} ${HEADER})

if(CMAKE_SYSTEM_NAME STREQUAL Darwin)
    target_link_libraries(decolorizer mac_utils sokol imgui himage)
else()
    target_link_libraries(decolorizer sokol imgui himage)
endif()

# Emscripten-specific linker options
if (CMAKE_SYSTEM_NAME STREQUAL Emscripten)
    # shell provided by the sokol samples
    target_link_options(decolorizer PRIVATE --bind -sEXTRA_EXPORTED_RUNTIME_METHODS=FS -sFETCH=1 -sASSERTIONS=1 
        -sTOTAL_MEMORY=1GB -sMALLOC=emmalloc --closure=1 --shell-file ../shell.html)
endif()
