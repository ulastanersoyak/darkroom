cmake_minimum_required(VERSION 3.12)
project(darkroom C)
add_compile_options(-g -Wall -Wextra -Wpedantic)
set(HEADERS
    src/include/bmp_image.h
    src/include/custom_math.h
)

set(SOURCES
    src/bmp_image.c
    src/custom_math.c
)
add_library(darkroom ${SOURCES} ${HEADERS})
target_include_directories(darkroom PUBLIC src/include)
target_link_libraries(darkroom PUBLIC m)

enable_testing()
add_executable(test_bmp_image tests/test_bmp_image.c)
target_link_libraries(test_bmp_image PRIVATE darkroom)
add_test(NAME bmp_image COMMAND test_bmp_image
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
