#include "Queue.h"

Queue *CreateQueue(int Capacity){
    Queue *NewQueue = (Queue*)malloc(sizeof(Queue));

    NewQueue->Capacity = Capacity;
    NewQueue->DataArray = (void**)malloc(sizeof(void*) * Capacity);
    NewQueue->Count = NewQueue->Front = NewQueue->Rear = 0;
    InitializeCriticalSection(&NewQueue->cs);

    return NewQueue;
}

void DestroyQueue(Queue* Q){
    for(int i=0; i<Q->Capacity; i++){
        Q->DataArray[i] = NULL;
    }
    free(Q->DataArray);
    DeleteCriticalSection(&Q->cs);
    free(Q);
}

BOOL Enqueue(Queue* Q, ElementType Item){
    EnterCriticalSection(&Q->cs);
    if(Q->Count == Q->Capacity){
        LeaveCriticalSection(&Q->cs);
        return FALSE;
    }

    Q->DataArray[Q->Rear] = Item;
    Q->Rear = (Q->Rear + 1) % Q->Capacity;
    Q->Count++;
    LeaveCriticalSection(&Q->cs);

    return TRUE;
}

ElementType Dequeue(Queue* Q){
    EnterCriticalSection(&Q->cs);

    if(Q->Count == 0){
        LeaveCriticalSection(&Q->cs);
        return NULL;
    }

    ElementType Item = Q->DataArray[Q->Front];
    Q->Front = (Q->Front + 1) % Q->Capacity;
    Q->Count--;
    LeaveCriticalSection(&Q->cs);

    return Item;
}
