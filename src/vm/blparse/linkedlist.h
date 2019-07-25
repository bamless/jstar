#ifndef LINKEDLIST_H
#define LINKEDLIST_H

#include "blconf.h"

#include <stdlib.h>

typedef struct LinkedList {
    void *elem;
    struct LinkedList *next;
} LinkedList;

#define foreach(node, list) for(LinkedList *node = list; node != NULL; node = node->next)

BLANG_API LinkedList *addElement(LinkedList *lst, void *elem);
BLANG_API size_t listLength(LinkedList *lst);
BLANG_API void freeLinkedList(LinkedList *lst);

#endif
