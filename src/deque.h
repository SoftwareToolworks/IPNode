/*
 * Original by Yash Tulsiani
 *
 * Converted to C by Software Toolworks
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdbool.h>

struct dnode;

typedef struct
{
    struct dnode *head;
    struct dnode *tail;
    unsigned int size;
} deque;

/* Creating */
deque *create_deque(void);

/* Adding */
void push_front(deque *, void *);
void push_back(deque *, void *);

/* Querying Deque */
void *front(deque *);
void *back(deque *);
void *get(deque *, unsigned int);
bool is_empty(deque *);
unsigned int size(deque *);

/* Removing */
void *pop_front(deque *);
void *pop_back(deque *);
void empty_deque(deque *);

#ifdef __cplusplus
}
#endif

