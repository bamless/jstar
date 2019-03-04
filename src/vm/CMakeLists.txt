FILE(GLOB_RECURSE SOURCES *.c "${PROJECT_SOURCE_DIR}/builtin/*.c") 
FILE(GLOB_RECURSE BLANG_SOURCES *bl "${PROJECT_SOURCE_DIR}/builtin/*.bl")

# create blang source headers names
set(BLANG_HEADERS)
foreach(bl ${BLANG_SOURCES})
	list(APPEND BLANG_HEADERS "${bl}.h")
endforeach()

# create commands to generate blang source headers
foreach(bl ${BLANG_SOURCES})
	add_custom_command(
		OUTPUT  "${bl}.h"
		COMMAND "${PROJECT_SOURCE_DIR}/../util/txt2incl.py" ${bl} "${bl}.h"
		DEPENDS ${bl}
	)
endforeach()

# create library
add_library(libblang ${SOURCES} ${BLANG_HEADERS})
set_target_properties(libblang PROPERTIES OUTPUT_NAME "blang")
