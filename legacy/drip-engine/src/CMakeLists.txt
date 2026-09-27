add_executable(drip main.cpp)
target_link_libraries(drip PRIVATE drip::app)

add_subdirectory(app)
add_subdirectory(common)
add_subdirectory(gfx)
add_subdirectory(simulation)
