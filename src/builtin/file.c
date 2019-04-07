#include "file.h"
#include "memory.h"
#include "object.h"
#include "vm.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#endif

#if defined (__unix__) || (defined (__APPLE__) && defined (__MACH__))
#include <sys/stat.h>
#endif

#define FIELD_FILE_HANDLE "_handle"
#define FIELD_FILE_CLOSED "_closed"

#define BL_SEEK_SET  0
#define BL_SEEK_CURR 1
#define BL_SEEK_END  2

// static helper functions

static ObjString *readline(BlangVM *vm, FILE *file) {
	size_t len = 0;
	size_t size = 256;
	ObjString *line = allocateString(vm, size);

	char *ret = fgets(line->data, size + 1, file);
	if(ret == NULL) {
		if(feof(file)) {
			reallocateString(vm, line, 0);
			return line;
		} else {
			return NULL;
		}
	}
	len = strlen(line->data);

	char *newLine;
	while((newLine = strchr(line->data, '\n')) == NULL) {
		char buf[256];

		ret = fgets(buf, sizeof(buf), file);
		if(ret == NULL) {
			if(feof(file)) {
				break;
			} else {
				return NULL;
			}
		}

		size_t bufLen = strlen(buf);
		while(len + bufLen >= size) {
			size_t newSize = size * 2;
			reallocateString(vm, line, newSize);
			size = newSize;
		}

		memcpy(line->data + len, buf, bufLen);
		len += bufLen;
	}

	if(line->length != len)
		reallocateString(vm, line, len);
	
	return line;
}

static int64_t getFileSize(FILE *stream) {
	int64_t fsize = -1;

#ifdef _WIN32
	int fd = _fileno(stream);
	if(fd < 0) {
		return -1;
	}

	HANDLE f = (HANDLE)_get_osfhandle(fd);
	if(f == INVALID_HANDLE_VALUE) {
		return -1;
	}

	DWORD lo = 0;
	DWORD hi = 0;
	lo = GetFileSize(f, &hi);
	fsize = (int64_t) (((uint64_t) hi) << 32) | lo;
#else
	int fd = fileno(stream);
	if(fd < 0) {
		return -1;
	}

	struct stat stat;
	if(fstat(fd, &stat)) {
		return -1;
	}

	fsize = (int64_t) stat.st_size;
#endif

	return fsize;
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
	if(!blCheckHandle(vm, -1, "_handle")) return false;
	if(!blCheckInt(vm, 1, "off") || !blCheckInt(vm, 2, "whence")) return false;

	FILE *f = (FILE*) blGetHandle(vm, -1);

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
	if(!blCheckHandle(vm, -1, "_handle")) return false;

	FILE *f = (FILE*) blGetHandle(vm, -1);

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
	if(!blCheckHandle(vm, -1, "_handle")) return false;

	FILE *f = (FILE*) blGetHandle(vm, -1);
	rewind(f);

	blPushNull(vm);
	return true;
}

NATIVE(bl_File_readAll) {
	if(!checkClosed(vm)) return false;

	if(!blGetField(vm, 0, FIELD_FILE_HANDLE)) return false;
	if(!blCheckHandle(vm, -1, "_handle")) return false;

	FILE *f = (FILE*) blGetHandle(vm, -1);

	long off = ftell(f);
	if(off == -1) {
		BL_RAISE(vm, "IOException", strerror(errno));
	}

	int64_t size = getFileSize(f) - off;
	if(size < 0) {
		blPushNull(vm);
		return true;
	}

	ObjString *data = allocateString(vm, size);

	if(fread(data->data, sizeof(char), size, f) < (size_t) size) {
		BL_RAISE(vm, "IOException", "Couldn't read the whole file.");
	}

	push(vm, OBJ_VAL(data));
	return true;
}

NATIVE(bl_File_readLine) {
	if(!checkClosed(vm)) return false;

	if(!blGetField(vm, 0, FIELD_FILE_HANDLE)) return false;
	if(!blCheckHandle(vm, -1, "_handle")) return false;

	FILE *f = (FILE*) blGetHandle(vm, -1);

	ObjString *line = readline(vm, f);
	if(line == NULL) {
		BL_RAISE(vm, "IOException", strerror(errno));
	}

	push(vm, OBJ_VAL(line));
	return true;
}

NATIVE(bl_File_close) {
	if(!blGetField(vm, 0, FIELD_FILE_HANDLE)) return false;
	if(!blCheckHandle(vm, -1, "_handle")) return false;

	FILE *f = (FILE*) blGetHandle(vm, -1);
	
	blPushBoolean(vm, true);
	blSetField(vm, 0, FIELD_FILE_CLOSED);

	if(fclose(f)) {
		BL_RAISE(vm, "IOException", strerror(errno));
	}

	blPushNull(vm);
	blSetField(vm, 0, FIELD_FILE_HANDLE);
	return true;
}

NATIVE(bl_File_size) {
	if(!checkClosed(vm)) return false;

	if(!blGetField(vm, 0, FIELD_FILE_HANDLE)) return false;
	if(!blCheckHandle(vm, -1, "_handle")) return false;
	FILE *f = (FILE*) blGetHandle(vm, -1);

	blPushNumber(vm, getFileSize(f));
	return true;
}
// } class File

// functions

NATIVE(bl_open) {
	const char *fname = blGetString(vm, 1);
	const char *m = blGetString(vm, 2);

	size_t mlen = strlen(m);
	if(mlen > 3 ||
	  (m[0] != 'r' && m[0] != 'w' && m[0] != 'a') ||
	  (mlen > 1 && (m[1] != 'b' && m[1] != '+')) ||
	  (mlen > 2 && m[2] != 'b'))
	{
		BL_RAISE(vm, "InvalidArgException", "invalid mode string \"%s\"", m);
	}

	FILE *f = fopen(fname, m);
	if(f == NULL) {
		if(errno == ENOENT) {
			BL_RAISE(vm, "FileNotFoundException", "Couldn't find file `%s`.", fname);
		}
		BL_RAISE(vm, "IOException", strerror(errno));
	}

	blPushHandle(vm, (void*) f);
	return true;
}
