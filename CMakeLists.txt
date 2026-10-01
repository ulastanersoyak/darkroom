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
