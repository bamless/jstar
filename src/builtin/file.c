#include "file.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

#if defined (__unix__) || (defined (__APPLE__) && defined (__MACH__))
#include <sys/stat.h>
#endif

#define FIELD_FILE_HANDLE "__handle"

// static helper functions

static char *readline(VM *vm, FILE *file, size_t *len) {
	*len = 0;
	size_t size = 256;
	char *line = ALLOC(vm, size);
	if(!line) goto error;

	char *ret = fgets(line, size - 1, file);
	if(ret == NULL) {
		if(feof(file)) {
			line = allocate(vm, line, size, 1);
			line[0] = '\0';
			size = 1;
			return line;
		} else {
			goto error;
		}
	}
	*len = strlen(line);

	char *newLine;
	while((newLine = strchr(line, '\n')) == NULL) {
		char buf[256];

		ret = fgets(buf, 255, file);
		if(ret == NULL) {
			if(feof(file)) {
				break;
			} else {
				goto error;
			}
		}

		size_t bufLen = strlen(buf);
		while(*len + bufLen >= size) {
			size_t newSize = size * 2;
			line = allocate(vm, line, size, newSize);
			size = newSize;
		}

		strcat(line, buf);
		*len += bufLen;
	}

	line[*len] = '\0';
	line = allocate(vm, line, size, *len + 1);
	return line;

error:
	FREEARRAY(vm, char, line, size);
	return NULL;
}

static int64_t getFileSize(FILE *stream) {
	int64_t fsize = -1;

#ifdef _WIN32
	int fd = _fileno(stream);
	if(fd < 0) {
		return -1;
	}

	HANDLE f = _get_osfhandle(fd);
	if(f == INVALID_HANDLE_VALUE) {
		return -1;
	}

	DWORD lo = 0;
	DWORD hi = 0;
	lo = GetFileSize(f, &hi);
	fsize = (int64_t) (((uint64_t) hi) << 32) | lo);
#else
	int fd = fileno(stream);
	if(fd < 0) {
		return NUM_VAL(-1);
	}

	struct stat stat;
	if(fstat(fd, &stat)) {
		return -1;
	}

	fsize = (int64_t) stat.st_size;
#endif

	return fsize;
}

// class File {

NATIVE(bl_File_readAll) {
	Value h;
	if(!blGetField(vm, BL_THIS, FIELD_FILE_HANDLE, &h) || !IS_HANDLE(h)) {
		return NULL_VAL;
	}

	FILE *f = (FILE*) AS_HANDLE(h);
	int64_t size = getFileSize(f) - ftell(f);
	if(size < 0) {
		return NULL_VAL;
	}

	char *data = ALLOC(vm, size);
	if(fread(data, 1, size, f) < (size_t) size) {
		FREEARRAY(vm, char, data, size);
		return NULL_VAL;
	}

	return OBJ_VAL(newStringFromBuf(vm, data, strlen(data)));
}

NATIVE(bl_File_readLine) {
	Value h;
	if(!blGetField(vm, BL_THIS, FIELD_FILE_HANDLE, &h) || !IS_HANDLE(h)) {
		return NULL_VAL;
	}

	FILE *f = (FILE*) AS_HANDLE(h);

	size_t length;
	char *line = readline(vm, f, &length);
	if(line == NULL) {
		return NULL_VAL;
	}

	return OBJ_VAL(newStringFromBuf(vm, line, length));
}

NATIVE(bl_File_close) {
	Value h;
	if(!blGetField(vm, BL_THIS, FIELD_FILE_HANDLE, &h) || !IS_HANDLE(h)) {
		return FALSE_VAL;
	}

	blSetField(vm, BL_THIS, FIELD_FILE_HANDLE, NULL_VAL);

	FILE *f = (void*) AS_HANDLE(h);
	if(fclose(f)) {
		return FALSE_VAL;
	}

	return TRUE_VAL;
}

NATIVE(bl_File_size) {
	Value h;
	if(!blGetField(vm, BL_THIS, FIELD_FILE_HANDLE, &h) || !IS_HANDLE(h)) {
		return NUM_VAL(-1);
	}

	FILE *stream = (FILE*) AS_HANDLE(h);

	return NUM_VAL(getFileSize(stream));
}

// } class File

// functions

NATIVE(bl_open) {
	FILE *f = fopen(AS_STRING(args[1])->data, AS_STRING(args[2])->data);
	if(f == NULL) return NULL_VAL;

	return HANDLE_VAL(f);
}
