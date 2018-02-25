#ifndef LINKEDLIST_H
#define LINKEDLIST_H

typedef struct LinkedList {
	void *elem;
	struct LinkedList *next;
} LinkedList;

#define foreach(node, list) \
	for(node = list; node != NULL; node = node->next)

LinkedList *addElement(LinkedList *lst, void *elem);
void freeLinkedList(LinkedList *lst);

#endif
