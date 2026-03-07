add_executable(bin2incl main.c)
target_link_libraries(bin2incl PRIVATE common)
target_include_directories(bin2incl PRIVATE ${PROJECT_BINARY_DIR} ${PROJECT_SOURCE_DIR}/profile)

if(LTO)
    set_target_properties(bin2incl PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()
