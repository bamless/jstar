#include "stringbuf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_LENGTH 16

static void sbuf_grow(StringBuffer *sbuf, size_t len);

void sbuf_create(StringBuffer *sbuf) {
	sbuf->buff = malloc(DEFAULT_LENGTH);
	if(!sbuf->buff) {
		fprintf(stderr, "%s\n", "Error: stringbuf: out of memory");
		abort();
	}

	sbuf->size = DEFAULT_LENGTH;
	sbuf->len = 0;
	sbuf->buff[0] = '\0';
}

void sbuf_destroy(StringBuffer *sbuf) {
	free(sbuf->buff);
}

char* sbuf_detach_and_destroy(StringBuffer *sbuf) {
	char *buf = sbuf->buff;
	return buf;
}

void sbuf_clear(StringBuffer *sbuf) {
	sbuf->len = 0;
	sbuf->buff[0] = '\0';
}

char* sbuf_get_backing_buf(StringBuffer *sbuf) {
	return sbuf->buff;
}

char* sbuf_strstr(StringBuffer *sbuf, const char *needle) {
	return strstr(sbuf->buff, needle);
}

size_t sbuf_get_len(StringBuffer *sbuf) {
	return sbuf->len;
}

bool sbuf_endswith(StringBuffer *sbuf, const char *str) {
	size_t str_len = strlen(str);
	if(sbuf->len < str_len) return 0;
	return strcmp(&sbuf->buff[sbuf->len - str_len], str) == 0;
}

void sbuf_append(StringBuffer *sbuf, const char *str, size_t len) {
	if(sbuf->len + len >= sbuf->size) sbuf_grow(sbuf, len + 1); //the >= and the +1 are for the terminating NUL
	memcpy(&sbuf->buff[sbuf->len], str, len);
	sbuf->len += len;
	sbuf->buff[sbuf->len] = '\0';
}

size_t sbuf_get_backing_size(StringBuffer *sbuf) {
	return sbuf->size;
}

char* sbuf_detach(StringBuffer *sbuf) {
	char *buf = sbuf->buff;
	sbuf->buff = malloc(DEFAULT_LENGTH);
	sbuf->size = DEFAULT_LENGTH;
	sbuf_clear(sbuf);
	return buf;
}

void sbuf_appendstr(StringBuffer *sbuf, const char *str) {
	sbuf_append(sbuf, str, strlen(str));
}

void sbuf_truncate(StringBuffer *sbuf, size_t len) {
	if(len >= sbuf->len) return;
	sbuf->len = len;
	sbuf->buff[sbuf->len] = '\0';
}

void sbuf_cut(StringBuffer *sbuf, size_t len) {
	if(len == 0 || len > sbuf->len) return;
	memmove(sbuf->buff, sbuf->buff + len, sbuf->len - len);
	sbuf->len -= len;
	sbuf->buff[sbuf->len] = '\0';
}

static void sbuf_grow(StringBuffer *sbuf, size_t len) {
	size_t new_size = sbuf->size;
	//multiply by 2 the size until it can hold sizeof(len) new data. Multiplying,
	//instead of growing at a constant rate, ensures constant amortized time complexity
	while(new_size < sbuf->len + len)
		new_size <<= 1;
	char *new_buff = realloc(sbuf->buff, new_size);
	if(!new_buff) {
		fprintf(stderr, "%s\n", "Error: stringbuf: out of memory");
		abort();
	}
	sbuf->size = new_size;
	sbuf->buff = new_buff;
}
