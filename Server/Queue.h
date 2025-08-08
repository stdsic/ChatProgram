#ifndef __QUEUE_H_
#define __QUEUE_H_
#include <windows.h>

typedef void* ElementType;

typedef struct tag_Queue{
    ElementType *DataArray;
    int Front, Rear, Capacity, Count;
    CRITICAL_SECTION cs;
}Queue;

Queue *CreateQueue(int Capacity);
void DestroyQueue(Queue* Q);
BOOL Enqueue(Queue* Q, ElementType Item);
ElementType Dequeue(Queue* Q);

#endif
