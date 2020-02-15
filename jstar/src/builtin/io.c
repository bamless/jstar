#ifdef _WIN32
    #define popen _popen
    #define pclose _pclose
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

static bool readline(JStarVM *vm, FILE *file) {
    char buf[512];
    char *ret = fgets(buf, sizeof(buf), file);
    if(ret == NULL) {
        if(feof(file)) {
            jsrPushNull(vm);
            return true;
        } else {
            return false;
        }
    }

    JStarBuffer b;
    jsrBufferInitSz(vm, &b, 16);
    jsrBufferAppendstr(&b, buf);

    char *newLine;
    while((newLine = strchr(b.data, '\n')) == NULL) {
        ret = fgets(buf, sizeof(buf), file);
        if(ret == NULL) {
            if(feof(file)) {
                break;
            } else {
                jsrBufferFree(&b);
                return false;
            }
        }

        jsrBufferAppendstr(&b, buf);
    }
    jsrBufferPush(&b);
    return true;
}

static int jsrSeek(FILE *file, long offset, int blWhence) {
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

JSR_NATIVE(jsr_File_new) {
    if(jsrIsNull(vm, 3)) {
        JSR_CHECK(String, 1, "path");
        JSR_CHECK(String, 2, "mode");
        
        const char *path = jsrGetString(vm, 1);
        const char *m = jsrGetString(vm, 2);

        size_t mlen = strlen(m);
        if(mlen > 3 || (m[0] != 'r' && m[0] != 'w' && m[0] != 'a') ||
          (mlen > 1 && (m[1] != 'b' && m[1] != '+')) || 
          (mlen > 2 && m[2] != 'b'))
        {
            JSR_RAISE(vm, "InvalidArgException", "invalid mode string \"%s\"", m);
        }

        FILE *f = fopen(path, m);
        if(f == NULL) {
            if(errno == ENOENT) {
                JSR_RAISE(vm, "FileNotFoundException", "Couldn't find file `%s`.", path);
            }
            JSR_RAISE(vm, "IOException", strerror(errno));
        }

        // this._handle = f
        jsrPushHandle(vm, (void *)f);
        jsrSetField(vm, 0, FIELD_FILE_HANDLE);

        //this._closed = false
        jsrPushBoolean(vm, false);
        jsrSetField(vm, 0, FIELD_FILE_CLOSED);
    } else if(jsrIsHandle(vm, 3)) {
        jsrSetField(vm, 0, FIELD_FILE_HANDLE);
        jsrPushBoolean(vm, false);
        jsrSetField(vm, 0, FIELD_FILE_CLOSED);
    } else {
        JSR_RAISE(vm, "TypeException", "Provided FILE handle is not valid");
    }

    // return `this`. required in native constructors
    jsrPushValue(vm, 0);
    return true;
}

static bool checkClosed(JStarVM *vm) {
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

    FILE *f = (FILE *)jsrGetHandle(vm, -1);

    double offset = jsrGetNumber(vm, 1);
    double whence = jsrGetNumber(vm, 2);

    if(whence != BL_SEEK_SET && whence != BL_SEEK_CURR && whence != BL_SEEK_END) {
        JSR_RAISE(vm, "InvalidArgException", "whence must be SEEK_SET, SEEK_CUR or SEEK_END");
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

    FILE *f = (FILE *)jsrGetHandle(vm, -1);

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

    FILE *f = (FILE *)jsrGetHandle(vm, -1);
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
    FILE *f = (FILE *)jsrGetHandle(vm, -1);

    JStarBuffer data;
    jsrBufferInitSz(vm, &data, bytes);

    size_t read;
    if((read = fread(data.data, 1, bytes, f)) < (size_t)bytes && ferror(f)) {
        jsrBufferFree(&data);
        JSR_RAISE(vm, "IOException", "Couldn't read the whole file.");
    }

    data.len = read;
    jsrBufferPush(&data);
    return true;
}

JSR_NATIVE(jsr_File_readAll) {
    if(!checkClosed(vm)) return false;
    if(!jsrGetField(vm, 0, FIELD_FILE_HANDLE)) return false;

    JSR_CHECK(Handle, -1, FIELD_FILE_HANDLE);

    FILE *f = (FILE *)jsrGetHandle(vm, -1);

    long off = ftell(f);
    if(off == -1) JSR_RAISE(vm, "IOException", strerror(errno));
    if(fseek(f, 0, SEEK_END)) JSR_RAISE(vm, "IOException", strerror(errno));

    long size = ftell(f) - off;
    if(size < 0) {
        jsrPushNull(vm);
        return true;
    }

    if(fseek(f, off, SEEK_SET)) JSR_RAISE(vm, "IOException", strerror(errno));

    JStarBuffer data;
    jsrBufferInitSz(vm, &data, size + 1);

    size_t read;
    if((read = fread(data.data, 1, size, f)) < (size_t)size && ferror(f)) {
        jsrBufferFree(&data);
        JSR_RAISE(vm, "IOException", "Couldn't read the whole file.");
    }

    data.len = read;
    jsrBufferPush(&data);
    return true;
}

JSR_NATIVE(jsr_File_readLine) {
    if(!checkClosed(vm)) return false;
    if(!jsrGetField(vm, 0, FIELD_FILE_HANDLE)) return false;
    JSR_CHECK(Handle, -1, FIELD_FILE_HANDLE);

    FILE *f = (FILE *)jsrGetHandle(vm, -1);

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

    FILE *f = (FILE *)jsrGetHandle(vm, -1);

    size_t datalen = jsrGetStringSz(vm, 1);
    const char *data = jsrGetString(vm, 1);

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

    FILE *f = (FILE *)jsrGetHandle(vm, -1);

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

    FILE *f = (FILE *)jsrGetHandle(vm, -1);

    fflush(f);

    jsrPushNull(vm);
    return true;
}
// } class File

// class __PFile {
JSR_NATIVE(jsr_PFile_close) {
    if(!checkClosed(vm)) return false;
    if(!jsrGetField(vm, 0, FIELD_FILE_HANDLE)) return false;
    JSR_CHECK(Handle, -1, FIELD_FILE_HANDLE);

    FILE *f = (FILE *)jsrGetHandle(vm, -1);

    jsrPushBoolean(vm, true);
    jsrSetField(vm, 0, FIELD_FILE_CLOSED);

    int ret;
    if((ret = pclose(f)) < 0) {
        JSR_RAISE(vm, "IOException", strerror(errno));
    }

    jsrPushNumber(vm, ret);
    return true;
}
// }

// functions

JSR_NATIVE(jsr_remove) {
    JSR_CHECK(String, 1, "path");
    if(remove(jsrGetString(vm, 1)) == -1) {
        JSR_RAISE(vm, "IOException", strerror(errno));
    }
    jsrPushNull(vm);
    return true;
}

JSR_NATIVE(jsr_rename) {
    JSR_CHECK(String, 1, "oldpath");
    JSR_CHECK(String, 2, "newpath");

    if(rename(jsrGetString(vm, 1), jsrGetString(vm, 2)) == -1) {
        JSR_RAISE(vm, "IOException", strerror(errno));
    }
    jsrPushNull(vm);
    return true;
}

JSR_NATIVE(jsr_popen) {
    JSR_CHECK(String, 1, "name");

    const char *pname = jsrGetString(vm, 1);
    const char *m = jsrGetString(vm, 2);

    if(strlen(m) != 1 || (m[0] != 'r' && m[1] != 'w')) {
        JSR_RAISE(vm, "InvalidArgException", "invalid mode string \"%s\"", m);
    }

    FILE *f = popen(pname, m);
    if(f == NULL) {
        JSR_RAISE(vm, "IOException", strerror(errno));
    }

    jsrGetGlobal(vm, NULL, "__PFile");
    jsrPushHandle(vm, f);
    if(jsrCall(vm, 1) != VM_EVAL_SUCCESS) return false;
    return true;
}

JSR_NATIVE(jsr_io_init) {
    if(!jsrGetGlobal(vm, "io", "File")) return false;

    // Set stdout
    jsrDup(vm);
    jsrPushNull(vm);
    jsrPushNull(vm);
    jsrPushHandle(vm, stdout);
    if(jsrCall(vm, 3) != VM_EVAL_SUCCESS) return false;
    jsrSetGlobal(vm, NULL, "stdout");
    jsrPop(vm);

    // Set stderr
    jsrDup(vm);
    jsrPushNull(vm);
    jsrPushNull(vm);
    jsrPushHandle(vm, stderr);
    if(jsrCall(vm, 3) != VM_EVAL_SUCCESS) return false;
    jsrSetGlobal(vm, NULL, "stderr");
    jsrPop(vm);

    // Set stdin
    jsrPushNull(vm);
    jsrPushNull(vm);
    jsrPushHandle(vm, stdin);
    if(jsrCall(vm, 3) != VM_EVAL_SUCCESS) return false;
    jsrSetGlobal(vm, NULL, "stdin");
    jsrPop(vm);

    jsrPushNull(vm);
    return true;
}