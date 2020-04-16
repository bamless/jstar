#ifndef LINKEDLIST_H
#define LINKEDLIST_H

#include <stdlib.h>

#include "jstarconf.h"

typedef struct LinkedList {
    void* elem;
    struct LinkedList* next;
} LinkedList;

#define foreach(node, list) for(LinkedList* node = list; node != NULL; node = node->next)

JSTAR_API LinkedList* addElement(LinkedList* lst, void* elem);
JSTAR_API size_t listLength(LinkedList* lst);
JSTAR_API void freeLinkedList(LinkedList* lst);

#endif
