/*
 * Yash Tulsiani
 */
#pragma once

struct dnode;

typedef struct
{
  struct dnode *head;
  struct dnode *tail;
  int size;
} deque;

/* Creating */
deque *create_deque(void);

/* Adding */
void push_front(deque *d, void *data);
void push_back(deque *d, void *data);

/* Querying Deque */
void *front(deque *d);
void *back(deque *d);
void *get(deque *d, int index);
int is_empty(deque *d);
int size(deque *d);

/* Removing */
void *pop_front(deque *d);
void *pop_back(deque *d);
void empty_deque(deque *d);
