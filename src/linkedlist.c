#include "linkedlist.h"

#include <stdlib.h>

LinkedList *addElement(LinkedList *lst, void *elem) {
	LinkedList *node = malloc(sizeof(*node));
	node->elem = elem;
	node->next = NULL;

	if(lst == NULL) return node;

	LinkedList *curr = lst;
	while(curr->next != NULL)
		curr = curr->next;

	curr->next = node;
	return lst;
}

void freeLinkedList(LinkedList *lst) {
	while(lst != NULL) {
		LinkedList *n = lst;
		lst = lst->next;
		free(n);
	}
}
