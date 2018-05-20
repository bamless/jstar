#ifndef FILE_H
#define FILE_H

#include "blang.h"

// interface File {

NATIVE(bl_File_readAll);
NATIVE(bl_File_readLine);
NATIVE(bl_File_read);
NATIVE(bl_File_close);
NATIVE(bl_File_size);

// } class File

// prototypes

NATIVE(bl_open);

#endif
