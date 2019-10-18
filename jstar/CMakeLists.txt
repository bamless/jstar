FILE(GLOB_RECURSE SOURCES src/*.c)
FILE(GLOB_RECURSE JSTAR_SOURCES src/*.jsr)

# libjstar version
set(LIBJSTAR_VERSION_MAJOR 1)
set(LIBJSTAR_VERSION_MINOR 0)
set(LIBJSTAR_VERSION_PATCH 0)
set(LIBJSTAR_VERSION_STRING "${LIBJSTAR_VERSION_MAJOR}.${LIBJSTAR_VERSION_MINOR}.${LIBJSTAR_VERSION_PATCH}")

# create jstar source headers names
set(JSTAR_HEADERS)
foreach(jsr ${JSTAR_SOURCES})
	list(APPEND JSTAR_HEADERS "${jsr}.h")
endforeach()

# create commands to generate jstar source headers
foreach(jsr ${JSTAR_SOURCES})
	add_custom_command(
		OUTPUT  "${jsr}.h"
		COMMAND python "${PROJECT_SOURCE_DIR}/util/txt2incl.py" ${jsr} "${jsr}.h"
		DEPENDS ${jsr}
	)
endforeach()

include_directories("${PROJECT_SOURCE_DIR}/jstar/src" "${PROJECT_SOURCE_DIR}/include")

# library's public headers
set(PUBLIC_HEADERS 
	${PROJECT_SOURCE_DIR}/include/jstarconf.h
	${PROJECT_SOURCE_DIR}/include/jstar.h
	${PROJECT_SOURCE_DIR}/include/jstar.hpp
)

# static library
add_library(libjstar_static STATIC ${SOURCES} ${JSTAR_HEADERS})
target_compile_definitions(libjstar_static PRIVATE JSTAR_STATIC)
set_target_properties(libjstar_static PROPERTIES 
	OUTPUT_NAME   "jstars" 
	PUBLIC_HEADER ${PUBLIC_HEADERS}
)

# find the math library if needed
set(EXTRA_LIBS)
if(UNIX)
	set(EXTRA_LIBS dl m)
endif()

add_library(libjstar  SHARED ${SOURCES} ${JSTAR_HEADERS})
target_link_libraries(libjstar ${EXTRA_LIBS})
set_target_properties(libjstar PROPERTIES 
	OUTPUT_NAME   "jstar"
	VERSION       ${LIBJSTAR_VERSION_STRING}
	SOVERSION     ${LIBJSTAR_VERSION_MAJOR} 
	PUBLIC_HEADER ${PUBLIC_HEADERS}
)