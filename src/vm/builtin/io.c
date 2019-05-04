// Check for popen, pclose
#ifdef __linux__

#define USE_POPEN
#define _GNU_SOURCE
#include <stdio.h>
#undef _GNU_SOURCE

#elif defined(_WIN32)

#define USE_POPEN
#define popen _popen
#define pclose _pclose

#elif defined(__unix__) || (defined (__APPLE__) && defined (__MACH__))

#define USE_POPEN
#define _POSIX_SOURCE
#include <stdio.h>
#undef _POSIX_SOURCE

#endif

#include "io.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define BL_SEEK_SET 0
#define BL_SEEK_CURR 1
#define BL_SEEK_END 2

// static helper functions

static bool readline(BlangVM *vm, FILE *file) {
    char buf[512];
    char *ret = fgets(buf, sizeof(buf), file);
    if(ret == NULL) {
        if(feof(file)) {
            blPushNull(vm);
            return true;
        } else {
            return false;
        }
    }

    BlBuffer b;
    blBufferInitSz(vm, &b, 16);
    blBufferAppendstr(&b, buf);

    char *newLine;
    while((newLine = strchr(b.data, '\n')) == NULL) {
        ret = fgets(buf, sizeof(buf), file);
        if(ret == NULL) {
            if(feof(file)) {
                break;
            } else {
                blBufferFree(&b);
                return false;
            }
        }

        blBufferAppendstr(&b, buf);
    }
    if(b.data[b.len - 1] == '\n') b.len--;
    blBufferPush(&b);
    return true;
}

static int blSeek(FILE *file, long offset, int blWhence) {
    int whence = 0;
    switch(blWhence) {
    case BL_SEEK_SET:
        whence = SEEK_SET;
        break;
    case BL_SEEK_CURR:
        whence = SEEK_CUR;
        break;
    case BL_SEEK_END:
        whence = SEEK_END;
        break;
    }
    return fseek(file, offset, whence);
}

// class File {

static bool checkClosed(BlangVM *vm) {
    if(!blGetField(vm, 0, FIELD_FILE_CLOSED)) return false;
    bool closed = blGetBoolean(vm, -1);
    if(closed) BL_RAISE(vm, "IOException", "closed file");
    return true;
}

NATIVE(bl_File_seek) {
    if(!checkClosed(vm)) return false;
    if(!blGetField(vm, 0, FIELD_FILE_HANDLE)) return false;
    if(!blCheckHandle(vm, -1, FIELD_FILE_HANDLE)) return false;
    if(!blCheckInt(vm, 1, "off") || !blCheckInt(vm, 2, "whence")) return false;

    FILE *f = (FILE *)blGetHandle(vm, -1);

    double offset = blGetNumber(vm, 1);
    double whence = blGetNumber(vm, 2);

    if(whence != BL_SEEK_SET && whence != BL_SEEK_CURR && whence != BL_SEEK_END) {
        BL_RAISE(vm, "InvalidArgException", "whence must be SEEK_SET, SEEK_CUR or SEEK_END");
    }

    if(blSeek(f, offset, whence)) {
        BL_RAISE(vm, "IOException", strerror(errno));
    }

    blPushNull(vm);
    return true;
}

NATIVE(bl_File_tell) {
    if(!checkClosed(vm)) return false;
    if(!blGetField(vm, 0, FIELD_FILE_HANDLE)) return false;
    if(!blCheckHandle(vm, -1, FIELD_FILE_HANDLE)) return false;

    FILE *f = (FILE *)blGetHandle(vm, -1);

    long off = ftell(f);
    if(off == -1) {
        BL_RAISE(vm, "IOException", strerror(errno));
    }

    blPushNumber(vm, off);
    return true;
}

NATIVE(bl_File_rewind) {
    if(!checkClosed(vm)) return false;
    if(!blGetField(vm, 0, FIELD_FILE_HANDLE)) return false;
    if(!blCheckHandle(vm, -1, FIELD_FILE_HANDLE)) return false;

    FILE *f = (FILE *)blGetHandle(vm, -1);
    rewind(f);

    blPushNull(vm);
    return true;
}

NATIVE(bl_File_read) {
    if(!checkClosed(vm)) return false;
    if(!blCheckInt(vm, 1, "bytes")) return false;
    if(!blGetField(vm, 0, FIELD_FILE_HANDLE)) return false;
    if(!blCheckHandle(vm, -1, FIELD_FILE_HANDLE)) return false;

    double bytes = blGetNumber(vm, 1);
    if(bytes < 0) BL_RAISE(vm, "InvalidArgException", "bytes must be >= 0");
    FILE *f = (FILE *)blGetHandle(vm, -1);

    BlBuffer data;
    blBufferInitSz(vm, &data, bytes);

    size_t read;
    if((read = fread(data.data, 1, bytes, f)) < (size_t)bytes && ferror(f)) {
        blBufferFree(&data);
        BL_RAISE(vm, "IOException", "Couldn't read the whole file.");
    }

    data.len = read;
    blBufferPush(&data);
    return true;
}

NATIVE(bl_File_readAll) {
    if(!checkClosed(vm)) return false;
    if(!blGetField(vm, 0, FIELD_FILE_HANDLE)) return false;
    if(!blCheckHandle(vm, -1, FIELD_FILE_HANDLE)) return false;

    FILE *f = (FILE *)blGetHandle(vm, -1);

    long off = ftell(f);
    if(off == -1) BL_RAISE(vm, "IOException", strerror(errno));
    if(fseek(f, 0, SEEK_END)) BL_RAISE(vm, "IOException", strerror(errno));

    long size = ftell(f) - off;
    if(size < 0) {
        blPushNull(vm);
        return true;
    }

    if(fseek(f, off, SEEK_SET)) BL_RAISE(vm, "IOException", strerror(errno));

    BlBuffer data;
    blBufferInitSz(vm, &data, size + 1);

    size_t read;
    if((read = fread(data.data, 1, size, f)) < (size_t)size && ferror(f)) {
        blBufferFree(&data);
        BL_RAISE(vm, "IOException", "Couldn't read the whole file.");
    }

    data.len = read;
    blBufferPush(&data);
    return true;
}

NATIVE(bl_File_readLine) {
    if(!checkClosed(vm)) return false;
    if(!blGetField(vm, 0, FIELD_FILE_HANDLE)) return false;
    if(!blCheckHandle(vm, -1, FIELD_FILE_HANDLE)) return false;

    FILE *f = (FILE *)blGetHandle(vm, -1);

    if(!readline(vm, f)) {
        BL_RAISE(vm, "IOException", strerror(errno));
    }

    return true;
}

NATIVE(bl_File_write) {
    if(!checkClosed(vm)) return false;
    if(!blCheckStr(vm, 1, "data")) return false;
    if(!blGetField(vm, 0, FIELD_FILE_HANDLE)) return false;
    if(!blCheckHandle(vm, -1, FIELD_FILE_HANDLE)) return false;

    FILE *f = (FILE *)blGetHandle(vm, -1);

    size_t datalen = blGetStringSz(vm, 1);
    const char *data = blGetString(vm, 1);

    if(fwrite(data, 1, datalen, f) < datalen) {
        BL_RAISE(vm, "IOException", strerror(errno));
    }

    blPushNull(vm);
    return true;
}

NATIVE(bl_File_close) {
    if(!blGetField(vm, 0, FIELD_FILE_HANDLE)) return false;
    if(!blCheckHandle(vm, -1, FIELD_FILE_HANDLE)) return false;

    FILE *f = (FILE *)blGetHandle(vm, -1);

    blPushBoolean(vm, true);
    blSetField(vm, 0, FIELD_FILE_CLOSED);

    if(fclose(f)) {
        BL_RAISE(vm, "IOException", strerror(errno));
    }

    blPushNull(vm);
    blSetField(vm, 0, FIELD_FILE_HANDLE);
    return true;
}

NATIVE(bl_File_flush) {
    if(!checkClosed(vm)) return false;
    if(!blGetField(vm, 0, FIELD_FILE_HANDLE)) return false;
    if(!blCheckHandle(vm, -1, FIELD_FILE_HANDLE)) return false;

    FILE *f = (FILE *)blGetHandle(vm, -1);

    fflush(f);

    blPushNull(vm);
    return true;
}
// } class File

// class __PFile {
#ifdef USE_POPEN

NATIVE(bl_PFile_close) {
    if(!checkClosed(vm)) return false;
    if(!blGetField(vm, 0, FIELD_FILE_HANDLE)) return false;
    if(!blCheckHandle(vm, -1, FIELD_FILE_HANDLE)) return false;

    FILE *f = (FILE *)blGetHandle(vm, -1);

    blPushBoolean(vm, true);
    blSetField(vm, 0, FIELD_FILE_CLOSED);

    if(pclose(f)) {
        BL_RAISE(vm, "IOException", strerror(errno));
    }

    blPushNull(vm);
    return true;
}

#else

NATIVE(bl_PFile_close) {
    BL_RAISE(vm, "Exception", "pclose not available on current system.");
}

#endif
// }

// functions

NATIVE(bl_open) {
    const char *fname = blGetString(vm, 1);
    const char *m = blGetString(vm, 2);

    size_t mlen = strlen(m);
    if(mlen > 3 || (m[0] != 'r' && m[0] != 'w' && m[0] != 'a') ||
       (mlen > 1 && (m[1] != 'b' && m[1] != '+')) || (mlen > 2 && m[2] != 'b')) {
        BL_RAISE(vm, "InvalidArgException", "invalid mode string \"%s\"", m);
    }

    FILE *f = fopen(fname, m);
    if(f == NULL) {
        if(errno == ENOENT) {
            BL_RAISE(vm, "FileNotFoundException", "Couldn't find file `%s`.", fname);
        }
        BL_RAISE(vm, "IOException", strerror(errno));
    }

    blPushHandle(vm, (void *)f);
    return true;
}

NATIVE(bl_remove) {
    if(!blCheckStr(vm, 1, "path")) return false;
    if(remove(blGetString(vm, 1)) == -1) {
        BL_RAISE(vm, "IOException", strerror(errno));
    }
    blPushNull(vm);
    return true;
}

NATIVE(bl_rename) {
    if(!blCheckStr(vm, 1, "oldpath") || !blCheckStr(vm, 2, "newpath")) {
        return false;
    }
    if(rename(blGetString(vm, 1), blGetString(vm, 2)) == -1) {
        BL_RAISE(vm, "IOException", strerror(errno));
    }
    blPushNull(vm);
    return true;
}

#ifdef USE_POPEN

NATIVE(bl_popen) {
    if(!blCheckStr(vm, 1, "name") || !blCheckStr(vm, 2, "mode")) {
        return false;
    }

    const char *pname = blGetString(vm, 1);
    const char *m = blGetString(vm, 2);

    if(strlen(m) != 1 || (m[0] != 'r' && m[1] != 'w')) {
        BL_RAISE(vm, "InvalidArgException", "invalid mode string \"%s\"", m);
    }

    FILE *f;
    if((f = popen(pname, m)) == NULL) {
        BL_RAISE(vm, "IOException", strerror(errno));
    }

    blGetGlobal(vm, NULL, "__PFile");
    blPushHandle(vm, f);
    if(blCall(vm, 1) != VM_EVAL_SUCCSESS) return false;
    return true;
}

#else

NATIVE(bl_popen) {
    BL_RAISE(vm, "Exception", "popen not available on current system.");
}

#endif