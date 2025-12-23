#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "headers.h"
#ifndef Engine
#define Engine
// Define process structure

// Circular Queue Implementation
typedef struct CircularQueue
{
    int front, rear, size;
    int capacity;
    Process *array;
} CircularQueue;

// Priority Queue Implementation
typedef struct PriorityQueue
{
    Process *array;
    int size;
    int capacity;
} PriorityQueue;

// Circular Queue Functions remain the same
CircularQueue *createCircularQueue(int capacity)
{
    CircularQueue *queue = (CircularQueue *)malloc(sizeof(CircularQueue));
    queue->capacity = capacity;
    queue->front = queue->rear = queue->size = 0;
    queue->array = (Process *)malloc(capacity * sizeof(Process));
    return queue;
}

int isCircularQueueFull(CircularQueue *queue)
{
    return queue->size == queue->capacity;
}

int isCircularQueueEmpty(CircularQueue *queue)
{
    return queue->size == 0;
}

void enqueueCircularQueue(CircularQueue *queue, Process process)
{
    if (isCircularQueueFull(queue))
    {
        printf("Circular Queue Overflow\n");
        return;
    }
    queue->array[queue->rear] = process;
    queue->rear = (queue->rear + 1) % queue->capacity;
    queue->size++;
}

Process dequeueCircularQueue(CircularQueue *queue)
{
    if (isCircularQueueEmpty(queue))
    {
        printf("Circular Queue Underflow\n");
        Process dummy = {-1, -1, -1, -1, -1, -1};
        return dummy;
    }
    Process process = queue->array[queue->front];
    queue->front = (queue->front + 1) % queue->capacity;
    queue->size--;
    return process;
}

// Priority Queue Functions
PriorityQueue *createPriorityQueue(int capacity)
{
    PriorityQueue *pq = (PriorityQueue *)malloc(sizeof(PriorityQueue));
    pq->capacity = capacity;
    pq->size = 0;
    pq->array = (Process *)malloc(capacity * sizeof(Process));
    return pq;
}

void swapProcesses(Process *a, Process *b)
{
    Process temp = *a;
    *a = *b;
    *b = temp;
}

// Modified heapify functions to consider arrival time

void heapifyUpPriority(PriorityQueue *pq, int index)
{
    while (index > 0)
    {
        int parent = (index - 1) / 2;
        // Compare priority first
        if (pq->array[index].priority < pq->array[parent].priority ||
            // If priorities are equal, compare arrival times
            (pq->array[index].priority == pq->array[parent].priority &&
             pq->array[index].arrival_time < pq->array[parent].arrival_time))
        {
            swapProcesses(&pq->array[index], &pq->array[parent]);
            index = parent;
        }
        else
        {
            break;
        }
    }
}

void heapifyDownPriority(PriorityQueue *pq, int index)
{
    int smallest = index;
    int left = 2 * index + 1;
    int right = 2 * index + 2;

    // Check left child
    if (left < pq->size &&
        (pq->array[left].priority < pq->array[smallest].priority ||
         (pq->array[left].priority == pq->array[smallest].priority &&
          pq->array[left].arrival_time < pq->array[smallest].arrival_time)))
        smallest = left;

    // Check right child
    if (right < pq->size &&
        (pq->array[right].priority < pq->array[smallest].priority ||
         (pq->array[right].priority == pq->array[smallest].priority &&
          pq->array[right].arrival_time < pq->array[smallest].arrival_time)))
        smallest = right;

    if (smallest != index)
    {
        swapProcesses(&pq->array[index], &pq->array[smallest]);
        heapifyDownPriority(pq, smallest);
    }
}

void heapifyUpRuntime(PriorityQueue *pq, int index)
{
    while (index > 0)
    {
        int parent = (index - 1) / 2;
        // Compare runtime first
        if (pq->array[index].runtime < pq->array[parent].runtime ||
           // If runtimes are equal, compare arrival times
           (pq->array[index].runtime == pq->array[parent].runtime &&
               pq->array[index].arrival_time < pq->array[parent].arrival_time)) {
           swapProcesses(&pq->array[index], &pq->array[parent]);
           index = parent;
        }
        else {
           break;
        }
     }
}

void heapifyDownRuntime(PriorityQueue *pq, int index)
{
    int smallest = index;
    int left = 2 * index + 1;
    int right = 2 * index + 2;

    // Check left child
     if (left < pq->size &&
         (pq->array[left].runtime < pq->array[smallest].runtime ||
             (pq->array[left].runtime == pq->array[smallest].runtime &&
                 pq->array[left].arrival_time < pq->array[smallest].arrival_time)))
         smallest = left;
    
     // Check right child
     if (right < pq->size &&
         (pq->array[right].runtime < pq->array[smallest].runtime ||
             (pq->array[right].runtime == pq->array[smallest].runtime &&
                 pq->array[right].arrival_time < pq->array[smallest].arrival_time)))
         smallest = right;
    if (smallest != index)
    {
        swapProcesses(&pq->array[index], &pq->array[smallest]);
        heapifyDownRuntime(pq, smallest);
    }
}

// Rest of the functions remain the same
void insertPriorityPriorityQueue(PriorityQueue *pq, Process process)
{
    if (pq->size == pq->capacity)
    {
        printf("Priority Queue Overflow\n");
        return;
    }
    pq->array[pq->size] = process;
    heapifyUpPriority(pq, pq->size);
    pq->size++;
}

void insertRuntimePriorityQueue(PriorityQueue *pq, Process process)
{
    if (pq->size == pq->capacity)
    {
        printf("Priority Queue Overflow\n");
        return;
    }
    pq->array[pq->size] = process;
    heapifyUpRuntime(pq, pq->size);
    pq->size++;
}

Process removePriorityPriorityQueue(PriorityQueue *pq)
{
    if (pq->size == 0)
    {
        printf("Priority Queue Underflow\n");
        Process dummy = {-1, -1, -1, -1};
        return dummy;
    }
    Process root = pq->array[0];
    pq->array[0] = pq->array[--pq->size];
    heapifyDownPriority(pq, 0);
    return root;
}

Process removeRuntimePriorityQueue(PriorityQueue *pq)
{
    if (pq->size == 0)
    {
        printf("Priority Queue Underflow\n");
        Process dummy = {-1, -1, -1, -1};
        return dummy;
    }
    Process root = pq->array[0];
    pq->array[0] = pq->array[--pq->size];
    heapifyDownRuntime(pq, 0);
    return root;
}

void destroyPriorityQueue(PriorityQueue *pq)
{
    if (pq != NULL)
    {
        if (pq->array != NULL)
            free(pq->array);
        free(pq);
    }
}
void destroyCircularQueue(CircularQueue *queue)
{
    if (queue != NULL)
    {
        if (queue->array != NULL)
        {
            free(queue->array);
        }
        free(queue);
    }
}

#endif