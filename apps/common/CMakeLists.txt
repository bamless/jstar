add_library(extlib OBJECT extlib.h)
set_source_files_properties(extlib.h PROPERTIES LANGUAGE C)
target_compile_definitions(extlib PRIVATE EXTLIB_IMPL)
target_include_directories(extlib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_library(path OBJECT path.h path.c)
target_link_libraries(path PRIVATE cwalk extlib)
target_include_directories(path PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(JSTAR_LTO)
    set_target_properties(extlib PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
    set_target_properties(path PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()
