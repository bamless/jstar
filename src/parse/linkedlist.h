#ifndef LINKEDLIST_H
#define LINKEDLIST_H

#include <stdlib.h>

typedef struct LinkedList {
    void *elem;
    struct LinkedList *next;
} LinkedList;

#define foreach(node, list) for(LinkedList *node = list; node != NULL; node = node->next)

LinkedList *addElement(LinkedList *lst, void *elem);
size_t listLength(LinkedList *lst);
void freeLinkedList(LinkedList *lst);

#endif
