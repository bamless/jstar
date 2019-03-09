#ifndef STRINGBUF_H
#define STRINGBUF_H

#include <stdlib.h>
#include <stdbool.h>

/*
 * Auto-resizing char buffer.
 */
typedef struct StringBuffer {
	char *buff;  /*The backing char array*/
	size_t size; /*The size of the backing array*/
	size_t len;  /*The length of the buffer*/
} StringBuffer;

/*Creates a new StringBuffer and returns a pointer to it*/
void sbuf_create(StringBuffer *sbuf);
/*Frees the resources associated with the stringbuffer*/
void sbuf_destroy(StringBuffer *sbuf);
/*Frees the stringbuffer without freeing the backing char array. The caller should
 *free the returned array when done using it.*/
char* sbuf_detach_and_destroy(StringBuffer *sbuf);

/*Empties the StringBuffer*/
void sbuf_clear(StringBuffer *sbuf);
/*Returns the backing array for use with other functions*/
char* sbuf_get_backing_buf(StringBuffer *sbuf);
/*Search for the string 'needle' in the stringbuffer. Returns a pointer to the start
 *of the first ocurrence or NULL if it fails.*/
char* sbuf_strstr(StringBuffer *sbuf, const char *needle);
/*Returns the length of the StringBuffer*/
size_t sbuf_get_len(StringBuffer *sbuf);
/*Returns the actual length of the backing dynamic char array*/
size_t sbuf_get_backing_size(StringBuffer *sbuf);
/*Returns the backing char array and detach it from the stringbuffer. The stringbuffer
 *can continue to be used without modifying the returned buffer. The caller should
 *free the returned buffer when done using it*/
char* sbuf_detach(StringBuffer *sbuf);
/*Returns 1 if the StringBuffer ends with the string 'str', 0 otherwise*/
bool sbuf_endswith(StringBuffer *sbuf, const char *str);
/*Appends a char buffer of length 'len' to the stringbuffer*/
void sbuf_append(StringBuffer *sbuf, const char *str, size_t len);
/*Appends a string (NUL terminated char array) to the stringbuffer*/
void sbuf_appendstr(StringBuffer *sbuf, const char *str);
/*Appends a char to the stringbuffer*/
void sbuf_appendchar(StringBuffer *sbuf, char c);
/*Replace char src with r starting from start*/
void sbuf_replacechar(StringBuffer *sb, size_t start, char src, char r);
/*It truncates the string at length 'len'*/
void sbuf_truncate(StringBuffer *sbuf, size_t len);
/*It shifts the buffer to the left by 'len' positions deleting all previous chars.*/
void sbuf_cut(StringBuffer *sbuf, size_t len);

#endif
