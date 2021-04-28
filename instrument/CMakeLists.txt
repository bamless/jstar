add_library(instrument STATIC instrumentor.h instrumentor.c)
set_property(TARGET instrument PROPERTY POSITION_INDEPENDENT_CODE ON)