FILE(GLOB_RECURSE SOURCES *.c)
FILE(GLOB_RECURSE BLANG_SOURCES *bl)

# libblang version
set(LIBBLANG_VERSION_MAJOR 1)
set(LIBBLANG_VERSION_MINOR 0)
set(LIBBLANG_VERSION_PATCH 0)
set(LIBBLANG_VERSION_STRING ${LIBBLANG_VERSION_MAJOR}.${LIBBLANG_VERSION_MINOR}.${LIBBLANG_VERSION_PATCH})

# create blang source headers names
set(BLANG_HEADERS)
foreach(bl ${BLANG_SOURCES})
	list(APPEND BLANG_HEADERS "${bl}.h")
endforeach()

# create commands to generate blang source headers
foreach(bl ${BLANG_SOURCES})
	add_custom_command(
		OUTPUT  "${bl}.h"
		COMMAND python2 "${PROJECT_SOURCE_DIR}/../util/txt2incl.py" ${bl} "${bl}.h"
		DEPENDS ${bl}
	)
endforeach()

include_directories("${PROJECT_SOURCE_DIR}/vm" "${PROJECT_SOURCE_DIR}/include")

# static library
add_library(libblang_static STATIC ${SOURCES} ${BLANG_HEADERS})
set_target_properties(libblang_static
	PROPERTIES 
	OUTPUT_NAME   "blang" 
	PUBLIC_HEADER "${PROJECT_SOURCE_DIR}/include/blang.h;${PROJECT_SOURCE_DIR}/include/blang.hpp;"
)

# shared library
add_library(libblang  SHARED ${SOURCES} ${BLANG_HEADERS})
set_target_properties(libblang 
	PROPERTIES 
	OUTPUT_NAME   "blang"
	VERSION       "${LIBBLANG_VERSION_STRING}"
	SOVERSION     "${LIBBLANG_VERSION_MAJOR}" 
	PUBLIC_HEADER "${PROJECT_SOURCE_DIR}/include/blang.h;${PROJECT_SOURCE_DIR}/include/blang.hpp;"
)