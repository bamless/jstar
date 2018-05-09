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
EXEC_NAME = blang

# Where to search for source code
SRC = src
# Folder in which the object files will be placed
BUILD = build
# Folder in which the binary will be generated
BIN = bin
# Folder in which the libraries will be generated
LIB = lib
# Where to install the binary file (optional)
INST_PATH = /usr/bin
LIB_INST_PATH = /usr/lib

# Path containing project libraries (optional)
LIBS_PATH = -Llib

VM_LIBS  = -lm
CLI_LIBS = $(VM_LIBS) -lreadline -l:libblang.a

# Path in wich static libraries will be placed (must be one of the path in LIBS_PATH or none).
# This will be used to relink the project if one of the static lib changes (optional).
STATIC_PATH = lib
# Path containing external header files (optional)
INCLUDES = -I$(SRC)/vm

#Linker flags
LDFLAGS = -Wl,-rpath=\$$ORIGIN/../lib/ #rpath for faster testing. Allows the built binary in bin/ to find all the shared libs
# Compiler flags
CFLAGS = -DNAN_TAGGING -std=c11 -Wall -Wextra -O3

# Source extension
SRC_EXT = c

###### SETTINGS END ######

SHARED_EXT = so
ifeq ($(OS),Windows_NT)
	SHARED_EXT = dll
endif
ifeq ($(OS),Darwin)
	SHARED_EXT = dylib
endif

ifeq ($(DBG_STRESS_GC),1)
	CFLAGS += -DDBG_STRESS_GC
endif
ifeq ($(DBG_PRINT_GC),1)
	CFLAGS += -DDBG_PRINT_GC
endif
ifeq ($(DBG_PRINT_EXEC),1)
	CFLAGS += -DDBG_PRINT_EXEC
endif

# Recursive wildcard, used to get all c files in SRC directory recursively
rwildcard = $(foreach d, $(wildcard $1*), $(call rwildcard,$d/,$2) \
						$(filter $(subst *,%,$2), $d))

VM_SOURCES  = $(call rwildcard, $(SRC)/vm,  *.$(SRC_EXT))
CLI_SOURCES = $(call rwildcard, $(SRC)/cli, *.$(SRC_EXT))
STATIC_LIBS = $(call rwildcard, $(STATIC_PATH), *.a)

VM_OBJECTS  = $(VM_SOURCES:$(SRC)/%.$(SRC_EXT)=$(BUILD)/%.o)
CLI_OBJECTS = $(CLI_SOURCES:$(SRC)/%.$(SRC_EXT)=$(BUILD)/%.o)

DEPEND_VM  = $(VM_OBJECTS:.o=.d)
DEPEND_CLI = $(CLI_OBJECTS:.o=.d)

# Main target, it creates the folders needed by the build and launches 'all' target
.PHONY: all
all: createdirs
	@echo "Beginning build..."
	@echo ""
	@$(MAKE) cli --no-print-directory
	@echo ""
	@echo "Build successful"

.PHONY: libs
libs: createdirs
	@echo "Beginning build..."
	@echo ""
	@$(MAKE) vm --no-print-directory
	@echo ""
	@echo "Build successful"

# Creates the needed directories
.PHONY: createdirs
createdirs:
	@echo "Generic Make File With Header Dependencies, Copyright (C) 2016 Fabrizio Pietrucci"
	@echo "Creating directories"
	@mkdir -p $(dir $(VM_OBJECTS))
	@mkdir -p $(dir $(CLI_OBJECTS))
	@mkdir -p $(LIB)
	@mkdir -p $(BIN)

.PHONY: vm
vm: $(LIB)/lib$(EXEC_NAME).a $(LIB)/lib$(EXEC_NAME).$(SHARED_EXT)

$(LIB)/lib$(EXEC_NAME).a: $(VM_OBJECTS)
	@echo "Creating $@..."
	@$(AR) rc $@ $^

$(LIB)/lib$(EXEC_NAME).$(SHARED_EXT): $(VM_OBJECTS)
	@echo "Creating $@..."
	@$(CC) $(CFLAGS) -shared $^ -o $@ $(VM_LIBS)

.PHONY: cli
cli: $(BIN)/$(EXEC_NAME)

# Links the object files into an executable
$(BIN)/$(EXEC_NAME): $(CLI_OBJECTS) $(STATIC_LIBS) $(LIB)/lib$(EXEC_NAME).a
	@echo "Linking $@..."
	@$(CC) $(CFLAGS) $(LDFLAGS) $(CLI_OBJECTS) -o $@ $(LIBS_PATH) $(CLI_LIBS)

# Rules for the source files. It compiles source files if obj files are outdated.
# It also creates haeder dependency files (.d files) used to add headers as
# dependencies for the object files with -include later.
$(BUILD)/%.o: $(SRC)/%.$(SRC_EXT)
	@echo "[CC] Compiling $< -> $@"
	@$(CC) -c $(CFLAGS) $(INCLUDES) -MP -MMD $< -o $@

# Header dependencies. Adds the rules in the .d files, if they exists, in order to
# add headers as dependencies of obj files (see .d files in BUILD for more info).
# This rules will be merged with the previous rules.
-include $(DEPEND_VM)
-include $(DEPEND_CLI)

.PHONY: install
install:
	@cp $(BIN)/$(EXEC_NAME) $(INST_PATH)
	@cp $(LIB)/lib$(EXEC_NAME).$(SHARED_EXT) $(LIB_INST_PATH)

.PHONY: uninstall
uninstall:
	@rm $(INST_PATH)/$(EXEC_NAME)
	@rm $(LIB_INST_PATH)/lib$(EXEC_NAME).$(SHARED_EXT)

# Removes all the build directories with obj files and executable
.PHONY: clean
clean:
	@echo "Deleting directories..."
	@rm -rf $(BIN) $(BUILD) $(LIB)

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
