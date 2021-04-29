add_library(profile profiler.h profiler.c)
set_property(TARGET profile PROPERTY POSITION_INDEPENDENT_CODE ON)
target_include_directories(profile PRIVATE ${PROJECT_BINARY_DIR})

configure_file (
    ${PROJECT_SOURCE_DIR}/cmake/profileconf.h.in
    ${PROJECT_BINARY_DIR}/profileconf.h
)

if(LTO)
    set_target_properties(profile PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()