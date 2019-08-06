FILE(GLOB_RECURSE SOURCES *.c)
FILE(GLOB_RECURSE JSTAR_SOURCES *.jsr)

# libjstar version
set(LIBJSTAR_VERSION_MAJOR 1)
set(LIBJSTAR_VERSION_MINOR 0)
set(LIBJSTAR_VERSION_PATCH 0)
set(LIBJSTAR_VERSION_STRING ${LIBJSTAR_VERSION_MAJOR}.${LIBJSTAR_VERSION_MINOR}.${LIBJSTAR_VERSION_PATCH})

# create jstar source headers names
set(JSTAR_HEADERS)
foreach(jsr ${JSTAR_SOURCES})
	list(APPEND JSTAR_HEADERS "${jsr}.h")
endforeach()

# create commands to generate jstar source headers
foreach(jsr ${JSTAR_SOURCES})
	add_custom_command(
		OUTPUT  "${jsr}.h"
		COMMAND python "${PROJECT_SOURCE_DIR}/../util/txt2incl.py" ${jsr} "${jsr}.h"
		DEPENDS ${jsr}
	)
endforeach()

include_directories("${PROJECT_SOURCE_DIR}/vm" "${PROJECT_SOURCE_DIR}/include")

# static library
add_library(libjstar_static STATIC ${SOURCES} ${JSTAR_HEADERS})
target_compile_definitions(libjstar_static PRIVATE JSTAR_STATIC)
set_target_properties(libjstar_static PROPERTIES 
	OUTPUT_NAME   "jstars" 
	PUBLIC_HEADER "${PROJECT_SOURCE_DIR}/include/jstarconf.h;${PROJECT_SOURCE_DIR}/include/jstar.h;${PROJECT_SOURCE_DIR}/include/jstar.hpp;"
)

# shared library
if(${CMAKE_CXX_COMPILER_ID} STREQUAL "GNU" OR ${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang")
	set(EXTRA_LIBS ${EXTRA_LIBS} m dl)
endif()

add_library(libjstar  SHARED ${SOURCES} ${JSTAR_HEADERS})
target_link_libraries(libjstar ${EXTRA_LIBS})
set_target_properties(libjstar PROPERTIES 
	OUTPUT_NAME   "jstar"
	VERSION       "${LIBJSTAR_VERSION_STRING}"
	SOVERSION     "${LIBJSTAR_VERSION_MAJOR}" 
	PUBLIC_HEADER "${PROJECT_SOURCE_DIR}/include/jstarconf.h;${PROJECT_SOURCE_DIR}/include/jstar.h;${PROJECT_SOURCE_DIR}/include/jstar.hpp;"
)