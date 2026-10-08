set(TARGET viewer)

add_executable(${TARGET})

target_sources(${TARGET}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
)

target_link_libraries(${TARGET}
    PRIVATE
        osm
        SDL3::SDL3
        imgui
)

if(BUILD_TESTING)
    add_test(NAME viewer_missing_argument COMMAND viewer)
    add_test(NAME viewer_missing_file COMMAND viewer "${PROJECT_SOURCE_DIR}/tests/fixtures/missing.osm")
    set_tests_properties(viewer_missing_argument viewer_missing_file
        PROPERTIES
            WILL_FAIL TRUE
    )
    add_test(NAME viewer_smoke COMMAND viewer "${PROJECT_SOURCE_DIR}/tests/fixtures/small.osm" --smoke-test)
    set_tests_properties(viewer_smoke
        PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_RENDER_DRIVER=software"
            TIMEOUT 15
    )
endif()
