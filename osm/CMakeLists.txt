# Embed the labels font from ImGui
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/assets/DroidSans.ttf" labelFontHex HEX)
string(REGEX REPLACE "(..)" "0x\\1," labelFontBytes "${labelFontHex}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/label_font_data.h"
    "static const unsigned char labelFontData[] = {${labelFontBytes}};\n"
)

set(TARGET osm)

add_library(${TARGET})

target_sources(${TARGET}
    PRIVATE
        result.h
        osm.h
        osm.cpp
        labels.h
        labels.cpp
        style.h
        style.cpp
        map_view.h
        map_view.cpp
)

target_include_directories(${TARGET}
    PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(${TARGET}
    PUBLIC
        blend2d::blend2d
    PRIVATE
        pugixml
)
