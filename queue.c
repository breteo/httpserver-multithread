#include "queue.h"
#include <stdio.h>
#include <stdlib.h>
#include <err.h>

int MAX;

int total = 0;
int rear = - 1;
int front = - 1;

struct entry* createList(int size){
  struct entry *n = malloc((size) * sizeof(struct entry));
  if(!n){
    errx(EXIT_FAILURE, "Malloc failed\n");
    exit(EXIT_FAILURE);
  }
  MAX = size;
  return n;
}

int numberOfEntries(){
  return total;
}

int insert(struct entry add_item, struct entry *queue_array)
{
    if (rear == MAX - 1){
    return 0;
    }
    else
    {
        if (front == - 1)
        /*If queue is initially empty */
        front = 0;
        rear = rear + 1;
        queue_array[rear] = add_item;
        total += 1;
        return 1;
    }
} /* End of insert() */
 
int delete(struct entry *queue_array)
{
    if (front == - 1 || front > rear)
    {
        return 0;
    }
    else
    {
        printf("Element deleted from queue is : %s\n", queue_array[front].filename);
        front = front + 1;
        total -= 1;
        return 1;
    }
} /* End of delete() */
 
void display(struct entry *queue_array)
{
    int i;
    if (front == - 1)
        printf("Queue is empty \n");
    else
    {
        printf("Queue is : ");
        for (i = front; i <= rear; i++)
            printf("%s ", queue_array[i].filename);
        printf("\n");
    }
} /* End of display() */
