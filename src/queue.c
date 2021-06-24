#include "work_q.h"
#include <stdlib.h>

Node_t* last = NULL; 
Node_t* first = NULL; 

void push(int *connfd) {
    Node_t *newnode = malloc(sizeof(Node_t)); 
    newnode->connfd= connfd; 
    newnode->next = NULL; 
    if (last == NULL) {
        first = newnode; 
    }
    else {
        last->connfd = newnode; 
    }
    last = newnode; 
}

int* pop(){
    
    if (first == NULL) {
        return NULL; 
    }
    else {
        int *value = first->connfd; 
        Node_t *temp = first; 
        first = first->next; 
        if (first == NULL) {
            last = NULL; 
        }
        free(temp); 
        return value; 
    }
}