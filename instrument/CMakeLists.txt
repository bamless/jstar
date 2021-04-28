add_library(instrument STATIC instrumentor.h instrumentor.c)
target_include_directories(instrument PUBLIC ${PROJECT_SOURCE_DIR}/instrument)
set_property(TARGET instrument PROPERTY POSITION_INDEPENDENT_CODE ON)