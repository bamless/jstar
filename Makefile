# *********************************************************************
#  Generic Makefile With Header Dependencies
# *********************************************************************
#  Author:         	(c) Fabrizio Pietrucci
#  License:       	See the end of this file for license information
#  Created:        	December 17, 2016

#  Last changed:   	@Date: 2017/2/21 11:13
#  Changed by:     	@Author: fabrizio pietrucci
#  Version:  		@Verision 1.0

####### SETTINGS #######

# The compiler
CC = gcc

# Executable name
EXEC_NAME = lang

# Where to search for source code
SRC = src
# Folder in which the object files will be placed
BUILD = build
# Folder in which the binary will be generated
BIN = bin
# Where to install the binary file (optional)
INST_PATH =

# Path containing project libraries (optional)
LIBS_PATH =
# Path in wich static libraries will be placed (must be one of the path in LIBS_PATH or none).
# This will be used to relink the project if one of the static lib changes (optional).
STATIC_PATH =
# Path containing external header files (optional)
INCLUDES =

#Linker flags
LDFLAGS =
# Compiler flags
CFLAGS = -O3 -Wall -Wextra -pedantic --std=c11
# Libraries
LIBS =

# Source extension
SRC_EXT = c

ifeq ($(DBG_STRESS_GC),1)
	CFLAGS += -DDBG_STRESS_GC
endif
ifeq ($(DBG_PRINT_GC),1)
	CFLAGS += -DDBG_PRINT_GC
endif

###### SETTINGS END ######

# Recursive wildcard, used to get all c files in SRC directory recursively
rwildcard = $(foreach d, $(wildcard $1*), $(call rwildcard,$d/,$2) \
						$(filter $(subst *,%,$2), $d))

# Get all static libraires in LIBS_PATH (used in order to relink
# the program if one of the static libs changes)
STATICLIBS = $(call rwildcard, $(STATIC_PATH), *.a)
# Get all the source files in SRC
SOURCES = $(call rwildcard, $(SRC), *.$(SRC_EXT))
# Set object files names from source file names (used in order
# to relink the program if one of the object file changes)
OBJECTS = $(SOURCES:$(SRC)/%.$(SRC_EXT)=$(BUILD)/%.o)
# The dependency files that will be used in order to add header dependencies
DEPEND = $(OBJECTS:.o=.d)

# Main target, it creates the folders needed by the build and launches 'all' target
.PHONY: build
build: createdirs
	@echo "Beginning build..."
	@echo ""
	@$(MAKE) all --no-print-directory
	@echo ""
	@echo "Build successful"

# Creates the needed directories
.PHONY: createdirs
createdirs:
	@echo "Generic Make File With Header Dependencies, Copyright (C) 2016 Fabrizio Pietrucci"
	@echo "Creating directories"
	@mkdir -p $(dir $(OBJECTS))
	@mkdir -p $(BIN)

all: $(BIN)/$(EXEC_NAME)

# Links the object files into an executable
$(BIN)/$(EXEC_NAME): $(OBJECTS) $(STATICLIBS)
	@echo "Linking $@..."
	@$(CC) $(CFLAGS) $(INCLUDES) $(OBJECTS) $(LDFLAGS) -o $@ $(LIBS_PATH) $(LIBS)

# Rules for the source files. It compiles source files if obj files are outdated.
# It also creates haeder dependency files (.d files) used to add headers as
# dependencies for the object files with -include later.
$(BUILD)/%.o: $(SRC)/%.$(SRC_EXT)
	@echo "[CC] Compiling $< -> $@"
	@$(CC) -c $(CFLAGS) $(INCLUDES) -MP -MMD $< -o $@

# Header dependencies. Adds the rules in the .d files, if they exists, in order to
# add headers as dependencies of obj files (see .d files in BUILD for more info).
# This rules will be merged with the previous rules.
-include $(DEPEND)

.PHONY: install
install:
	@cp $(BIN)/$(EXEC_NAME) $(INST_PATH)

.PHONY: uninstall
uninstall:
	@rm $(INST_PATH)/$(EXEC_NAME)

# Removes all the build directories with obj files and executable
.PHONY: clean
clean:
	@echo "Deleting directories..."
	@rm -rf $(BIN) $(BUILD)

# Copyright (C) 2016 Fabrizio Pietrucci

# This program is free software: you can redistribute it and/or
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.

# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
