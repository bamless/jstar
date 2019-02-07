#include "file.h"

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

#define FIELD_FILE_HANDLE "__handle"

#define BL_SEEK_SET  0
#define BL_SEEK_CURR 1
#define BL_SEEK_END  2

// static helper functions

static char *readline(BlangVM *vm, FILE *file, size_t *len) {
	*len = 0;
	size_t size = 256;
	char *line = GC_ALLOC(vm, size);
	if(!line) goto error;

	char *ret = fgets(line, size - 1, file);
	if(ret == NULL) {
		if(feof(file)) {
			line = GCallocate(vm, line, size, 1);
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
			line = GCallocate(vm, line, size, newSize);
			size = newSize;
		}

		strcat(line, buf);
		*len += bufLen;
	}

	line[*len] = '\0';
	line = GCallocate(vm, line, size, *len + 1);
	return line;

error:
	GC_FREEARRAY(vm, char, line, size);
	return NULL;
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

NATIVE(bl_File_seek) {
	Value h;
	if(!blGetField(vm, BL_THIS, FIELD_FILE_HANDLE, &h) || !IS_HANDLE(h)) {
		BL_RETURN(NULL_VAL);
	}
	if(!IS_INT(args[1])){
		BL_RAISE_EXCEPTION(vm, "InvalidArgException", "off must be an integer");
	}
	if(!IS_INT(args[2])) {
		BL_RAISE_EXCEPTION(vm, "InvalidArgException", "whence must be an integer");
	}

	FILE *f = (FILE*) AS_HANDLE(h);

	double offset = AS_NUM(args[1]);
	double whence = AS_NUM(args[2]);

	if(whence != BL_SEEK_SET && whence != BL_SEEK_CURR && whence != BL_SEEK_END) {
		BL_RAISE_EXCEPTION(vm, "InvalidArgException",
			"whence must be SEEK_SET, SEEK_CUR or SEEK_END");
	}

	if(blSeek(f, offset, whence)) {
		if(errno == EINVAL)
			BL_RAISE_EXCEPTION(vm, "IOException", "resulting offset would be negative.");
		else
			BL_RAISE_EXCEPTION(vm, "IOException", "an I/O error occurred.");
	}

	BL_RETURN(NULL_VAL);
}

NATIVE(bl_File_setpos) {
	Value fwdArgs[] = {args[0], args[1], NUM_VAL(BL_SEEK_SET)};
	return bl_File_seek(vm, fwdArgs, ret);
}

NATIVE(bl_File_tell) {
	Value h;
	if(!blGetField(vm, BL_THIS, FIELD_FILE_HANDLE, &h) || !IS_HANDLE(h)) {
		BL_RETURN(NULL_VAL);
	}

	FILE *f = (FILE*) AS_HANDLE(h);
	BL_RETURN(NUM_VAL(ftell(f)));
}

NATIVE(bl_File_rewind) {
	Value h;
	if(!blGetField(vm, BL_THIS, FIELD_FILE_HANDLE, &h) || !IS_HANDLE(h)) {
		BL_RETURN(NULL_VAL);
	}

	FILE *f = (FILE*) AS_HANDLE(h);
	rewind(f);
	BL_RETURN(NULL_VAL);
}

NATIVE(bl_File_readAll) {
	Value h;
	if(!blGetField(vm, BL_THIS, FIELD_FILE_HANDLE, &h) || !IS_HANDLE(h)) {
		BL_RETURN(NULL_VAL);
	}

	FILE *f = (FILE*) AS_HANDLE(h);
	int64_t size = getFileSize(f) - ftell(f);
	if(size < 0) {
		BL_RETURN(NULL_VAL);
	}

	char *data = GC_ALLOC(vm, size + 1);
	if(fread(data, sizeof(char), size, f) < (size_t) size) {
		GC_FREEARRAY(vm, char, data, size);
		BL_RETURN(NULL_VAL);
	}
	data[size] = '\0';

	BL_RETURN(OBJ_VAL(newStringFromBuf(vm, data, strlen(data))));
}

NATIVE(bl_File_readLine) {
	Value h;
	if(!blGetField(vm, BL_THIS, FIELD_FILE_HANDLE, &h) || !IS_HANDLE(h)) {
		BL_RETURN(NULL_VAL);
	}

	FILE *f = (FILE*) AS_HANDLE(h);

	size_t length;
	char *line = readline(vm, f, &length);
	if(line == NULL) {
		BL_RETURN(NULL_VAL);
	}

	BL_RETURN(OBJ_VAL(newStringFromBuf(vm, line, length)));
}

NATIVE(bl_File_close) {
	Value h;
	if(!blGetField(vm, BL_THIS, FIELD_FILE_HANDLE, &h) || !IS_HANDLE(h)) {
		BL_RETURN(NULL_VAL);
	}

	blSetField(vm, BL_THIS, FIELD_FILE_HANDLE, NULL_VAL);

	FILE *f = (void*) AS_HANDLE(h);
	if(fclose(f)) {
		BL_RAISE_EXCEPTION(vm, "IOException", "An I/O error occurred.");
	}

	BL_RETURN(NULL_VAL);
}

NATIVE(bl_File_size) {
	Value h;
	if(!blGetField(vm, BL_THIS, FIELD_FILE_HANDLE, &h) || !IS_HANDLE(h)) {
		BL_RETURN(NUM_VAL(-1));
	}

	FILE *stream = (FILE*) AS_HANDLE(h);

	BL_RETURN(NUM_VAL(getFileSize(stream)));
}

// } class File

// functions

NATIVE(bl_open) {
	char *fname = AS_STRING(args[1])->data;
	char *m = AS_STRING(args[2])->data;

	size_t mlen = strlen(m);
	if(mlen > 3 ||
	  (m[0] != 'r' && m[0] != 'w' && m[0] != 'a') ||
	  (mlen > 1 && (m[1] != 'b' && m[1] != '+')) ||
	  (mlen > 2 && m[2] != 'b'))
	{
		BL_RAISE_EXCEPTION(vm, "InvalidArgException", "invalid mode string \"%s\"", m);
	}

	FILE *f = fopen(AS_STRING(args[1])->data, m);
	if(f == NULL) {
		BL_RAISE_EXCEPTION(vm, "FileNotFoundException", "Couldn't find file `%s`.", fname);
	}

	BL_RETURN(HANDLE_VAL(f));
}
