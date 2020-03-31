#include "jsrparse/linkedlist.h"

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

size_t listLength(LinkedList *lst) {
    size_t l = 0;
    while(lst != NULL) {
        l++;
        lst = lst->next;
    }
    return l;
}

void freeLinkedList(LinkedList *lst) {
    while(lst != NULL) {
        LinkedList *n = lst;
        lst = lst->next;
        free(n);
    }
}
