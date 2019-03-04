# find third party libraries
find_library(LIB_READLINE readline)
find_library(LIB_MATH m)

# add executable and libblang dependency
add_executable(blang cli.c)
target_link_libraries(blang libblang ${LIB_READLINE} ${LIB_MATH})
