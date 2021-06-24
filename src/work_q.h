#ifndef MYQUEUE_H_
#define MYQUEUE_H_

struct Node
{
    struct Node* next; 
    int *connfd; 
};
typedef struct Node Node_t; 

void push(int *connfd); 
int *pop(); 


#endif