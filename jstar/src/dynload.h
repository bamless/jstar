#ifndef DYNLOAD_H
#define DYNLOAD_H

void *dynload(const char *path);
void dynfree(void *handle);
void *dynsim(void *handle, const char *symbol);

#endif