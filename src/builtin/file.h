#ifndef FILE_H
#define FILE_H

#include "native.h"

#include <stdio.h>

// interface File {

NATIVE(bl_File_readLine);
NATIVE(bl_File_close);

// } class File

// prototypes

NATIVE(bl_open);

#endif
