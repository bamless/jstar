FILE(GLOB_RECURSE SOURCES *.c)
FILE(GLOB_RECURSE BLANG_SOURCES *bl)

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

# ---- Install Target ----

# static library
add_library(libblang_static STATIC ${SOURCES} ${BLANG_HEADERS})
set_target_properties(libblang_static
	PROPERTIES 
	OUTPUT_NAME "blang" 
	PUBLIC_HEADER "${PROJECT_SOURCE_DIR}/include/blang.h;${PROJECT_SOURCE_DIR}/include/blang.hpp;"
)

# shared library
add_library(libblang  SHARED ${SOURCES} ${BLANG_HEADERS})
set_target_properties(libblang 
	PROPERTIES 
	OUTPUT_NAME "blang" 
	PUBLIC_HEADER "${PROJECT_SOURCE_DIR}/include/blang.h;${PROJECT_SOURCE_DIR}/include/blang.hpp;"
)

#install libblang along with blang headers
install(TARGETS libblang 
	LIBRARY DESTINATION "${CMAKE_INSTALL_PREFIX}/lib"
	PUBLIC_HEADER DESTINATION "${CMAKE_INSTALL_PREFIX}/include"
)

#install blparse headers to provide parsing faciliuties to the user
install(DIRECTORY "${PROJECT_SOURCE_DIR}/vm/blparse"
		DESTINATION "${CMAKE_INSTALL_PREFIX}/include"
	    FILES_MATCHING PATTERN "*.h"
)
