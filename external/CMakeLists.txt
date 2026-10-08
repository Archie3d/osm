##
## Blend2D
##
set(BLEND2D_DIR "${CMAKE_CURRENT_SOURCE_DIR}/blend2d")
set(ASMJIT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/asmjit")
set(BLEND2D_STATIC TRUE)
include("${BLEND2D_DIR}/CMakeLists.txt")

##
## Pugixml
##
add_subdirectory(pugixml)

##
## SDL3
##
set(SDL_SHARED OFF CACHE BOOL "SDL_SHARED" FORCE)
set(SDL_STATIC ON CACHE BOOL "SDL_STATIC" FORCE)
add_subdirectory(SDL EXCLUDE_FROM_ALL)

##
## ImGUI
##
set(IMGUI_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/imgui)

set(IMGUI_SRC
    ${IMGUI_SRC_DIR}/imgui.cpp
    ${IMGUI_SRC_DIR}/imgui_draw.cpp
    ${IMGUI_SRC_DIR}/imgui_tables.cpp
    ${IMGUI_SRC_DIR}/imgui_widgets.cpp
    ${IMGUI_SRC_DIR}/misc/cpp/imgui_stdlib.cpp
    ${IMGUI_SRC_DIR}/backends/imgui_impl_sdl3.cpp
    ${IMGUI_SRC_DIR}/backends/imgui_impl_sdlrenderer3.cpp
)

add_library(imgui STATIC ${IMGUI_SRC})

target_include_directories(imgui PUBLIC
    ${IMGUI_SRC_DIR}
    ${IMGUI_SRC_DIR}/backends
)

target_link_libraries(imgui PRIVATE SDL3::SDL3)
