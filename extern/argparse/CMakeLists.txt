project(argparse)

add_library(argparse argparse.c)
target_include_directories(argparse PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
