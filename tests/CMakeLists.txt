
add_executable(osm_tests ${CMAKE_CURRENT_SOURCE_DIR}/osm_tests.cpp)
target_link_libraries(osm_tests PRIVATE osm)
target_compile_definitions(osm_tests PRIVATE OSM_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/small.osm")

add_test(NAME osm_loader COMMAND osm_tests loader)
add_test(NAME osm_renderer COMMAND osm_tests renderer)
add_test(NAME osm_areas COMMAND osm_tests areas)
add_test(NAME osm_labels COMMAND osm_tests labels)
add_test(NAME osm_view COMMAND osm_tests view)
add_test(NAME osm_resize COMMAND osm_tests resize)
