add_executable(bin2incl main.c)
target_link_libraries(bin2incl PRIVATE extlib)

if(JSTAR_LTO)
    set_target_properties(bin2incl PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()
