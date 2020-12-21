#include "io.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "util.h"

#if defined(JSTAR_POSIX)
    #define USE_POPEN
#elif defined(JSTAR_WINDOWS)
    #define USE_POPEN
    #define popen  _popen
    #define pclose _pclose
#endif

// Synchronized to Seek enum in io.jsr
#define JSR_SEEK_SET 0
#define JSR_SEEK_CUR 1
#define JSR_SEEK_END 2

// static helper functions

static bool readline(JStarVM* vm, FILE* file) {
    char buf[4096];

    char* line = fgets(buf, sizeof(buf), file);
    if(line == NULL) {
        if(feof(file)) {
            jsrPushNull(vm);
            return true;
        } else {
            return false;
        }
    }

    JStarBuffer data;
    jsrBufferInit(vm, &data);
    jsrBufferAppendStr(&data, buf);

    while(strrchr(buf, '\n') == NULL) {
        line = fgets(buf, sizeof(buf), file);
        if(line == NULL) {
            if(feof(file)) {
                break;
            } else {
                jsrBufferFree(&data);
                return false;
            }
        }
        jsrBufferAppendStr(&data, buf);
    }

    jsrBufferPush(&data);
    return true;
}

static int jsrSeek(FILE* file, long offset, int jsrWhence) {
    int whence = 0;
    switch(jsrWhence) {
    case JSR_SEEK_SET:
        whence = SEEK_SET;
        break;
    case JSR_SEEK_CUR:
        whence = SEEK_CUR;
        break;
    case JSR_SEEK_END:
        whence = SEEK_END;
        break;
    default:
        UNREACHABLE();
        break;
    }
    return fseek(file, offset, whence);
}

static bool checkModeString(const char* mode) {
    if(strcmp("r", mode) == 0 || strcmp("r+", mode) == 0 || strcmp("w", mode) == 0 ||
       strcmp("w+", mode) == 0 || strcmp("a", mode) == 0 || strcmp("a+", mode) == 0) {
        return true;
    }
    return false;
}

// class File
JSR_NATIVE(jsr_File_new) {
    if(jsrIsNull(vm, 3)) {
        JSR_CHECK(String, 1, "path");
        JSR_CHECK(String, 2, "mode");

        const char* path = jsrGetString(vm, 1);
        const char* mode = jsrGetString(vm, 2);

        if(!checkModeString(mode)) {
            JSR_RAISE(vm, "InvalidArgException", "invalid mode string `%s`", mode);
        }

        FILE* f = fopen(path, mode);
        if(f == NULL) {
            if(errno == ENOENT) {
                JSR_RAISE(vm, "FileNotFoundException", "Couldn't find file `%s`", path);
            } else {
                JSR_RAISE(vm, "IOException", "%s: %s", path, strerror(errno));
            }
        }

        jsrPushHandle(vm, (void*)f);
        jsrSetField(vm, 0, FIELD_FILE_HANDLE);

        jsrPushBoolean(vm, false);
        jsrSetField(vm, 0, FIELD_FILE_CLOSED);
    } else if(jsrIsHandle(vm, 3)) {
        jsrSetField(vm, 0, FIELD_FILE_HANDLE);
        jsrPushBoolean(vm, false);
        jsrSetField(vm, 0, FIELD_FILE_CLOSED);
    } else {
        JSR_RAISE(vm, "TypeException", "Provided FILE* handle is not valid");
    }

    // return `this`. required in native constructors
    jsrPushValue(vm, 0);
    return true;
}

static bool checkClosed(JStarVM* vm) {
    if(!jsrGetField(vm, 0, FIELD_FILE_CLOSED)) return false;
    bool closed = jsrGetBoolean(vm, -1);
    if(closed) JSR_RAISE(vm, "IOException", "closed file");
    return true;
}

JSR_NATIVE(jsr_File_seek) {
    if(!checkClosed(vm)) return false;
    if(!jsrGetField(vm, 0, FIELD_FILE_HANDLE)) return false;
    JSR_CHECK(Handle, -1, FIELD_FILE_HANDLE);
    JSR_CHECK(Int, 1, "off");
    JSR_CHECK(Int, 2, "whence");

    FILE* f = (FILE*)jsrGetHandle(vm, -1);
    int offset = jsrGetNumber(vm, 1);
    int whence = jsrGetNumber(vm, 2);

    if(whence < JSR_SEEK_SET || whence > JSR_SEEK_END) {
        JSR_RAISE(vm, "InvalidArgException", "Invalid whence (%d)", whence);
    }

    if(jsrSeek(f, offset, whence)) {
        JSR_RAISE(vm, "IOException", strerror(errno));
    }

    jsrPushNull(vm);
    return true;
}

JSR_NATIVE(jsr_File_tell) {
    if(!checkClosed(vm)) return false;
    if(!jsrGetField(vm, 0, FIELD_FILE_HANDLE)) return false;
    JSR_CHECK(Handle, -1, FIELD_FILE_HANDLE);

    FILE* f = (FILE*)jsrGetHandle(vm, -1);

    long off = ftell(f);
    if(off == -1) {
        JSR_RAISE(vm, "IOException", strerror(errno));
    }

    jsrPushNumber(vm, off);
    return true;
}

JSR_NATIVE(jsr_File_rewind) {
    if(!checkClosed(vm)) return false;
    if(!jsrGetField(vm, 0, FIELD_FILE_HANDLE)) return false;
    JSR_CHECK(Handle, -1, FIELD_FILE_HANDLE);

    FILE* f = (FILE*)jsrGetHandle(vm, -1);
    rewind(f);

    jsrPushNull(vm);
    return true;
}

JSR_NATIVE(jsr_File_read) {
    if(!checkClosed(vm)) return false;
    if(!jsrGetField(vm, 0, FIELD_FILE_HANDLE)) return false;
    JSR_CHECK(Handle, -1, FIELD_FILE_HANDLE);
    JSR_CHECK(Int, 1, "bytes");

    double bytes = jsrGetNumber(vm, 1);
    if(bytes < 0) JSR_RAISE(vm, "InvalidArgException", "bytes must be >= 0");
    FILE* f = (FILE*)jsrGetHandle(vm, -1);

    JStarBuffer data;
    jsrBufferInitCapacity(vm, &data, bytes);

    size_t read = fread(data.data, 1, bytes, f);
    if(read < (size_t)bytes && ferror(f)) {
        jsrBufferFree(&data);
        JSR_RAISE(vm, "IOException", strerror(errno));
    }

    data.size = read;
    jsrBufferPush(&data);
    return true;
}

JSR_NATIVE(jsr_File_readAll) {
    if(!checkClosed(vm)) return false;
    if(!jsrGetField(vm, 0, FIELD_FILE_HANDLE)) return false;
    JSR_CHECK(Handle, -1, FIELD_FILE_HANDLE);

    FILE* f = (FILE*)jsrGetHandle(vm, -1);

    JStarBuffer data;
    jsrBufferInitCapacity(vm, &data, 512);

    size_t read;
    char buf[4096];
    while((read = fread(buf, 1, sizeof(buf), f)) == sizeof(buf)) {
        jsrBufferAppend(&data, buf, read);
    }

    if(feof(f) && read > 0) {
        jsrBufferAppend(&data, buf, read);
    }

    if(ferror(f)) {
        jsrBufferFree(&data);
        JSR_RAISE(vm, "IOException", strerror(errno));
    }

    jsrBufferPush(&data);
    return true;
}

JSR_NATIVE(jsr_File_readLine) {
    if(!checkClosed(vm)) return false;
    if(!jsrGetField(vm, 0, FIELD_FILE_HANDLE)) return false;
    JSR_CHECK(Handle, -1, FIELD_FILE_HANDLE);

    FILE* f = (FILE*)jsrGetHandle(vm, -1);
    if(!readline(vm, f)) {
        JSR_RAISE(vm, "IOException", strerror(errno));
    }

    return true;
}

JSR_NATIVE(jsr_File_write) {
    if(!checkClosed(vm)) return false;
    if(!jsrGetField(vm, 0, FIELD_FILE_HANDLE)) return false;
    JSR_CHECK(Handle, -1, FIELD_FILE_HANDLE);
    JSR_CHECK(String, 1, "data");

    FILE* f = (FILE*)jsrGetHandle(vm, -1);
    size_t datalen = jsrGetStringSz(vm, 1);
    const char* data = jsrGetString(vm, 1);

    if(fwrite(data, 1, datalen, f) < datalen) {
        JSR_RAISE(vm, "IOException", strerror(errno));
    }

    jsrPushNull(vm);
    return true;
}

JSR_NATIVE(jsr_File_close) {
    if(!checkClosed(vm)) return false;
    if(!jsrGetField(vm, 0, FIELD_FILE_HANDLE)) return false;
    JSR_CHECK(Handle, -1, FIELD_FILE_HANDLE);

    FILE* f = (FILE*)jsrGetHandle(vm, -1);

    jsrPushBoolean(vm, true);
    jsrSetField(vm, 0, FIELD_FILE_CLOSED);

    if(fclose(f)) {
        JSR_RAISE(vm, "IOException", strerror(errno));
    }

    jsrPushNull(vm);
    jsrSetField(vm, 0, FIELD_FILE_HANDLE);
    return true;
}

JSR_NATIVE(jsr_File_flush) {
    if(!checkClosed(vm)) return false;
    if(!jsrGetField(vm, 0, FIELD_FILE_HANDLE)) return false;
    JSR_CHECK(Handle, -1, FIELD_FILE_HANDLE);
    FILE* f = (FILE*)jsrGetHandle(vm, -1);
    if(fflush(f) == EOF) JSR_RAISE(vm, "IOException", strerror(errno));
    jsrPushNull(vm);
    return true;
}
// end

// class Popen
JSR_NATIVE(jsr_Popen_new) {
#ifdef USE_POPEN
    JSR_CHECK(String, 1, "name");
    JSR_CHECK(String, 2, "mode");

    const char* pname = jsrGetString(vm, 1);
    const char* mode = jsrGetString(vm, 2);

    if(strlen(mode) != 1 || (mode[0] != 'r' && mode[0] != 'w')) {
        JSR_RAISE(vm, "InvalidArgException", "invalid mode string `%s`", mode);
    }

    FILE* f = popen(pname, mode);
    if(f == NULL) {
        JSR_RAISE(vm, "IOException", "%s: %s", pname, strerror(errno));
    }

    jsrPushHandle(vm, f);
    jsrSetField(vm, 0, FIELD_FILE_HANDLE);

    jsrPushBoolean(vm, false);
    jsrSetField(vm, 0, FIELD_FILE_CLOSED);

    jsrPushValue(vm, 0);
    return true;
#else
    JSR_RAISE(vm, "NotImplementedException", "Popen not supported on current system.");
#endif
}

JSR_NATIVE(jsr_Popen_close) {
#ifdef USE_POPEN
    if(!checkClosed(vm)) return false;
    if(!jsrGetField(vm, 0, FIELD_FILE_HANDLE)) return false;
    JSR_CHECK(Handle, -1, FIELD_FILE_HANDLE);

    FILE* f = (FILE*)jsrGetHandle(vm, -1);

    jsrPushBoolean(vm, true);
    jsrSetField(vm, 0, FIELD_FILE_CLOSED);

    int ret;
    if((ret = pclose(f)) < 0) {
        JSR_RAISE(vm, "IOException", strerror(errno));
    }

    jsrPushNumber(vm, ret);
    return true;
#else
    JSR_RAISE(vm, "NotImplementedException", "Popen not supported on current system.");
#endif
}
// end

// Functions

JSR_NATIVE(jsr_remove) {
    JSR_CHECK(String, 1, "path");
    if(remove(jsrGetString(vm, 1)) == -1) {
        JSR_RAISE(vm, "IOException", "%s: %s", jsrGetString(vm, 1), strerror(errno));
    }
    jsrPushNull(vm);
    return true;
}

JSR_NATIVE(jsr_rename) {
    JSR_CHECK(String, 1, "oldpath");
    JSR_CHECK(String, 2, "newpath");
    if(rename(jsrGetString(vm, 1), jsrGetString(vm, 2)) == -1) {
        JSR_RAISE(vm, "IOException", "%s: %s", jsrGetString(vm, 1), strerror(errno));
    }
    jsrPushNull(vm);
    return true;
}

static bool createStdFile(JStarVM* vm, const char* name, FILE* stdfile) {
    if(!jsrGetGlobal(vm, NULL, "File")) return false;
    jsrPushNull(vm);
    jsrPushNull(vm);
    jsrPushHandle(vm, stdfile);
    if(jsrCall(vm, 3) != JSR_SUCCESS) return false;
    jsrSetGlobal(vm, NULL, name);
    jsrPop(vm);
    return true;
}

JSR_NATIVE(jsr_io_init) {
    // Set stdout, stderr and stdin
    if(!createStdFile(vm, "stdout", stdout)) return false;
    if(!createStdFile(vm, "stderr", stderr)) return false;
    if(!createStdFile(vm, "stdin", stdin)) return false;
    jsrPushNull(vm);
    return true;
}