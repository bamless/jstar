# Static library
add_library(common STATIC path.h path.c)
target_link_libraries(common PRIVATE cwalk)
target_include_directories(common
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE
        ${PROJECT_BINARY_DIR}
        ${PROJECT_SOURCE_DIR}/profile
    )

# Enable link-time optimization if supported
if(LTO)
    set_target_properties(common PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()
