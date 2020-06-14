add_library(argparse argparse.c)

# Enable link-time optimization if supported
if(LTOAvailable)
	set_property(TARGET argparse PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()
