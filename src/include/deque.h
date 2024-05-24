/*
 * Copyright (C) 2024 The pgmoneta community
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list
 * of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this
 * list of conditions and the following disclaimer in the documentation and/or other
 * materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may
 * be used to endorse or promote products derived from this software without specific
 * prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 * TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef PGMONETA_DEQUE_H
#define PGMONETA_DEQUE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

enum node_type {
   NodeInt,
   NodeString,
   NodeBool,
   NodeRef,
};

struct deque_node
{
   enum node_type type;
   char* data;
   char* tag;
   struct deque_node* next;
   struct deque_node* prev;
};

struct deque
{
   uint32_t size;
   struct deque_node* start;
   struct deque_node* end;
};

/**
 * Create a deque
 * @param deque The deque
 * @return 0 if success, otherwise 1
 */
int
pgmoneta_deque_create(struct deque** deque);

/**
 * Add a string node to deque's tail, the string and tag are copied if not NULL
 * @param deque The deque
 * @param data The nullable string data
 * @param tag The tag, optional
 * @return 0 if success, otherwise 1
 */
int
pgmoneta_deque_offer_string(struct deque* deque, char* data, char* tag);

/**
 * Add an integer node to deque's tail, the tag is copied if not NULL
 * @param deque The deque
 * @param data The int data
 * @param tag The tag, optional
 * @return 0 if success, otherwise 1
 */
int
pgmoneta_deque_offer_int(struct deque* deque, int data, char* tag);

/**
 * Add a bool node to deque's tail, the tag is copied if not NULL
 * @param deque The deque
 * @param data The bool data
 * @param tag The tag, optional
 * @return 0 if success, otherwise 1
 */
int
pgmoneta_deque_offer_bool(struct deque* deque, bool data, char* tag);

/**
 * Add a object reference node to deque's tail, that tag is copied if not NULL
 * @param deque The deque
 * @param data The object pointer
 * @param tag The tag, optional
 * @return 0 if success, otherwise 1
 */
int
pgmoneta_deque_offer_ref(struct deque* deque, void* data, char* tag);

/**
 * Retrieve and remove the node from deque's head
 * @param deque The deque
 * @return The node if deque's not empty, otherwise NULL
 */
struct deque_node*
pgmoneta_deque_poll(struct deque* deque);

/**
 * Retrieve but not remove the node from deque's head
 * @param deque The deque
 * @return The node if deque's not empty, otherwise NULL
 */
struct deque_node*
pgmoneta_deque_peek(struct deque* deque);

/**
 * Retrieve and remove the int value from deque's head
 * @param deque The deque
 * @return The value if deque's not empty and the node type matches, otherwise 0
 */
int
pgmoneta_deque_poll_int(struct deque* deque);

/**
 * Retrieve but not remove the int value from deque's head
 * @param deque The deque
 * @return The value if deque's not empty and the node type matches, otherwise 0
 */
int
pgmoneta_deque_peek_int(struct deque* deque);

/**
 * Retrieve and remove the string value from deque's head, note that the string is copied so it must be freed explicitly
 * @param deque The deque
 * @return The value if deque's not empty and the node type matches, otherwise NULL
 */
char*
pgmoneta_deque_poll_string(struct deque* deque);

/**
 * Retrieve but not remove the string value from deque's head
 * @param deque The deque
 * @return The value if deque's not empty and the node type matches, otherwise NULL
 */
char*
pgmoneta_deque_peek_string(struct deque* deque);

/**
 * Retrieve and remove the bool value from deque's head
 * @param deque The deque
 * @return The value if deque's not empty and the node type matches, otherwise false
 */
bool
pgmoneta_deque_poll_bool(struct deque* deque);

/**
 * Retrieve but not remove the bool value from deque's head
 * @param deque The deque
 * @return The value if deque's not empty and the node type matches, otherwise false
 */
bool
pgmoneta_deque_peek_bool(struct deque* deque);

/**
 * Retrieve and remove the object pointer from deque's head
 * @param deque The deque
 * @return The value if deque's not empty and the node type matches, otherwise NULL
 */
void*
pgmoneta_deque_poll_ref(struct deque* deque);

/**
 * Retrieve but not remove the object pointer from deque's head
 * @param deque The deque
 * @return The value if deque's not empty and the node type matches, otherwise NULL
 */
void*
pgmoneta_deque_peek_ref(struct deque* deque);

/**
 * Check if the deque is empty
 * @param deque The deque
 * @return true if deque size is 0, otherwise false
 */
bool
pgmoneta_deque_empty(struct deque* deque);

/**
 * Destroy the deque and free its and its nodes' memory
 * @param deque The deque
 */
void
pgmoneta_deque_destroy(struct deque* deque);

/**
 * Remove a node from the deque.
 * @param deque The deque
 * @param node The node
 * @return Next node of the deleted node
 */
struct deque_node*
pgmoneta_deque_node_remove(struct deque* deque, struct deque_node* node);

#ifdef __cplusplus
}
#endif

#endif
