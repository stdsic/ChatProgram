#ifndef __QUEUE_H_
#define __QUEUE_H_

typedef void* ElementType;

typedef struct tag_Queue{
    ElementType *DataArray;
    int Front, Rear, Capacity;
}Queue;

Queue *CreateQueue(int Capacity){
    Queue *NewQueue = (Queue*)malloc(sizeof(Queue));

    NewQueue->Capacity = Capacity;
    NewQueue->DataArray = (void**)malloc(sizeof(void*) * Capacity);
    NewQueue->Front = NewQueue->Rear = 0;

    return Queue;
}

#endif
