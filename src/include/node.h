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

#ifndef PGMONETA_NODE_H
#define PGMONETA_NODE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <stdbool.h>

#define NODE_TYPE_STRING 0
#define NODE_TYPE_INT    1
#define NODE_TYPE_BOOL   2

/** @struct node
 * Defines a node
 */
struct node
{
   char type;         /**< The node type */
   void* data;        /**< The node data */
   char* tag;         /**< The node tag */
   struct node* next; /**< The next node pointer */
};

/**
 * Create a node string
 * @param s The string
 * @param tag The tag
 * @param result The result
 * @return 0 upon success, otherwise 1
 */
int pgmoneta_create_node_string(char* s, char* t, struct node** result);

/**
 * Create a node integer
 * @param val The integer value
 * @param t The tag
 * @param result The result
 * @return 0 upon success, otherwise 1
 */
int pgmoneta_create_node_int(int val, char* t, struct node** result);

/**
 * Create a node boolean
 * @param val The boolean value
 * @param t The tag
 * @param result The result
 * @return 0 upon success, otherwise 1
 */
int pgmoneta_create_node_bool(bool val, char* t, struct node** result);

/**
 * Get a node string
 * @param chain The node chain
 * @param t The tag
 * @return The string value
 */
char* pgmoneta_get_node_string(struct node* chain, char* t);

/**
 * Get a node integer
 * @param chain The node chain
 * @param t The tag
 * @return The integer value
 */
int pgmoneta_get_node_int(struct node* chain, char* t);

/**
 * Get a node boolean
 * @param chain The node chain
 * @param t The tag
 * @return The boolean value
 */
bool pgmoneta_get_node_bool(struct node* chain, char* t);

/**
 * Append a node node to the chain.
 * @param chain The node chain
 * @param node The node node
 */
void pgmoneta_append_node(struct node** chain, struct node* node);

/**
 * List the nodes
 * @param chain The node chain
 * @param input Are the nodes input (true), or output (false)
 */
void
pgmoneta_list_nodes(struct node* chain, bool input);

/**
 * Delete the node
 * @param in The node
 * @return 0 upon success, otherwise 1
 */
int pgmoneta_free_nodes(struct node* in);

#ifdef __cplusplus
}
#endif

#endif
