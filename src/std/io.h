#ifndef FILE_H
#define FILE_H

#include "jstar.h"

// class File {
#define FIELD_FILE_HANDLE "_handle"
#define FIELD_FILE_CLOSED "_closed"

JSR_NATIVE(jsr_File_new);
JSR_NATIVE(jsr_File_read);
JSR_NATIVE(jsr_File_readAll);
JSR_NATIVE(jsr_File_readLine);
JSR_NATIVE(jsr_File_write);
JSR_NATIVE(jsr_File_close);
JSR_NATIVE(jsr_File_seek);
JSR_NATIVE(jsr_File_tell);
JSR_NATIVE(jsr_File_rewind);
JSR_NATIVE(jsr_File_flush);
// } class File

// class Popen {
JSR_NATIVE(jsr_Popen_new);
JSR_NATIVE(jsr_Popen_close);
// }

// prototypes

JSR_NATIVE(jsr_remove);
JSR_NATIVE(jsr_rename);
JSR_NATIVE(jsr_io_init);

#endif
