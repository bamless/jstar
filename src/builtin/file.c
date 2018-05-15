#include "file.h"

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

// class File {

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

// } class File

// functions

NATIVE(bl_open) {
	FILE *f = fopen(AS_STRING(args[1])->data, AS_STRING(args[2])->data);
	if(f == NULL) return NULL_VAL;

	return HANDLE_VAL(f);
}
