add_executable(bin2incl main.c)
target_link_libraries(bin2incl PRIVATE common)
target_include_directories(bin2incl PRIVATE ${PROJECT_BINARY_DIR} ${PROJECT_SOURCE_DIR}/profile)
