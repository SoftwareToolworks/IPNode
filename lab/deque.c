/*
 * Yash Tulsiani
 */

#include <stdlib.h>

#include "deque.h"

typedef struct dnode
{
    struct dnode *prev;
    struct dnode *next;
    void *data;
} node;

/** create_node
 *
 * Helper function that creates a node by allocating memory for it on the heap.
 * Be sure to set its pointers to NULL.
 *
 * @param data a void pointer to data the user wants to store in the deque
 * @return a node
 */
static node *create_node(void *data)
{
    /// @todo Implement changing the return value!
    node *newNode = (node *)calloc(1, sizeof(node));

    // error
    if (newNode == NULL)
    {
        return NULL;
    }

    newNode->data = data;
    newNode->prev = NULL;
    newNode->next = NULL;

    return newNode;
}

/** create_deque
 *
 * Creates a deque by allocating memory for it on the heap.
 * Be sure to initialize size to zero and head/tail to NULL.
 *
 * @return an empty deque
 */
deque *create_deque(void)
{
    deque *d = (deque *)calloc(1, sizeof(deque));
    
    // error
    if (d == NULL)
    {
        return NULL;
    }
    
    d->head = NULL;
    d->tail = NULL;
    d->size = 0;
    
    return d;
}

/** push_front
 *
 * Adds the data to the front of the deque.
 *
 * @param d a pointer to the deque structure.
 * @param data pointer to data the user wants to store in the deque.
 */
void push_front(deque *d, void *data)
{
    /// @todo Implement
    if (d == NULL)
    {
        return;
    }
    node *newNode = create_node(data);

    if (d->size == 0)
    {
        d->head = newNode;
        d->tail = newNode;
    }
    else
    {
        newNode->next = d->head;
        d->head->prev = newNode;
        d->head = newNode;
    }

    d->size++;
}

/** push_back
 *
 * Adds the data to the back of the deque.
 *
 * @param d a pointer to the deque structure.
 * @param data pointer to data the user wants to store in the deque.
 */
void push_back(deque *d, void *data)
{
    /// @todo Implement
    if (d == NULL)
    {
        return;
    }
    node *newNode = create_node(data);
    if (d->size == 0)
    {
        d->head = newNode;
        d->tail = newNode;
    }
    else
    {
        newNode->prev = d->tail;
        d->tail->next = newNode;
        d->tail = newNode;
    }
    d->size++;
}

/** front
 *
 * Gets the data at the front of the deque
 * If the deque is empty return NULL.
 *
 * @param d a pointer to the deque
 * @return The data at the first node in the deque or NULL.
 */
void *front(deque *d)
{
    if (d == NULL || d->size == 0)
    {
        return NULL;
    }
    else
    {
        return d->head->data;
    }
}

/** back
 *
 * Gets the data at the "end" of the deque
 * If the deque is empty return NULL.
 *
 * @param d a pointer to the deque structure
 * @return The data at the last node in the deque or NULL.
 */
void *back(deque *d)
{
    if (d == NULL || d->size == 0)
    {
        return NULL;
    }
    else
    {
        return d->tail->data;
    }
}

/** get
 *
 * Gets the data at the specified index in the deque
 *
 * @param d a pointer to the deque structure
 * @param index 0-based, starting from the head.
 * @return The data from the specified index in the deque or NULL if index is
 *         out of range of the deque.
 */
void *get(deque *d, int index)
{
    if (d == NULL || index < 0 || index >= d->size)
    {
        return NULL;
    }

    node *getNode = d->head;
    
    for (int i = 0; i < index; i++)
    {
        getNode = getNode->next;
    }
    
    return getNode->data;
}

/** pop_front
 *
 * Removes the node at the front of the deque, and returns its data to the user
 *
 * @param d a pointer to the deque.
 * @return The data in the first node, or NULL if the deque is NULL or empty
 */
void *pop_front(deque *d)
{
    if (d == NULL || d->size == 0)
    {
        return NULL;
    }

    node *returnNode = d->head;
    void *returnData = returnNode->data;
    d->head = d->head->next;

    if (d->size > 1)
        d->head->prev = NULL;

    free(returnNode);
    d->size--;

    return returnData;
}

/** pop_back
 *
 * Removes the node at the end of the deque, and returns its data to the user
 *
 * @param d a pointer to the deque.
 * @return The data in the first node, or NULL if the deque is NULL or empty
 */
void *pop_back(deque *d)
{
    if (d == NULL || d->size == 0)
    {
        return NULL;
    }

    node *returnNode = d->tail;
    void *returnData = returnNode->data;
    d->tail = d->tail->prev;

    if (d->size > 1)
        d->tail->next = NULL;

    free(returnNode);
    d->size--;

    return returnData;
}

/** size
 *
 * Gets the size of the deque
 *
 * @param d a pointer to the deque structure
 * @return The size of the deque
 */
int size(deque *d)
{
    if (d == NULL)
    {
        return 0;
    }

    return d->size;
}

/** is_empty
 *
 * Checks to see if the deque is empty.
 *
 * @param d a pointer to the deque structure
 * @return 1 if the deque is indeed empty, or 0 otherwise.
 */
int is_empty(deque *d)
{
    if (d == NULL)
    {
        return 0;
    }

    if (d->size == 0 || d->head == NULL)
    {
        return 1;
    }

    return 0;
}

/** empty_deque
 *
 * Empties the deque. After this is called, the deque should be empty.
 * This does not free the deque struct itself, just all nodes and data within.
 *
 * @param d a pointer to the deque structure
 */
void empty_deque(deque *d)
{
    if (d == NULL)
    {
        return;
    }

    while (d->size != 0)
    {
        free(pop_front(d));
    }
}
