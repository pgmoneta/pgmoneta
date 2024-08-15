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

#ifndef PGMONETA_JSON_H
#define PGMONETA_JSON_H

#ifdef __cplusplus
extern "C" {
#endif

/* pgmoneta */
#include <pgmoneta.h>
#include <deque.h>
#include <value.h>

/* System */
#include <stdarg.h>

enum json_type {
   JSONUnknown,
   JSONItem,
   JSONArray
};

// This is just a subset of all possible state during parsing,
// since we basically only look into the item array in json,
// some state transition will be fast-forwarded
enum parse_state {
   KeyStart,
   KeyEnd,
   ValueStart,
   ValueEnd,
   ArrayStart,
   ArrayEnd,
   ItemStart,
   ItemEnd,
   InvalidState,
};

/** @struct json
 * Defines a JSON structure
 */
struct json
{
   // a json object can only be item or array
   enum json_type type;            /**< The json object type */
   // if the object is an array, it can have at most one json element
   void* elements;                /**< The json elements, could be an array or some kv pairs */
};

/** @struct json_reader
 * Defines a JSON reader
 */
struct json_reader
{
   struct stream_buffer* buffer; /**< The buffer */
   int fd;                       /**< The file descriptor */
   enum parse_state state; /**< The current reader state of the JSON reader */
};

/** @struct json_iterator
 * Defines a JSON iterator
 */
struct json_iterator
{
   void* iter;            /**< The internal iterator */
   struct json* obj;      /**< The json object */
   char* key;             /**< The current key, if it's json item */
   struct value* value;   /**< The current value or entry */
};

/**
 * Initialize the json reader
 * @param path The json file path
 * @param reader The reader
 * @return 0 if success, 1 if otherwise
 */
int
pgmoneta_json_reader_init(char* path, struct json_reader** reader);

/**
 * Close and free the json reader
 * @param reader The reader
 */
void
pgmoneta_json_reader_close(struct json_reader* reader);

/**
 * Navigate the reader to the target json object(array or item) according to the key prefix array,
 * it currently does not handle escape character
 * @param reader The json reader
 * @param key_path The key path as an array leading to the key to the array
 * @param key_path_length The length of the prefix
 * @return 0 if found, 1 if otherwise
 */
int
pgmoneta_json_locate(struct json_reader* reader, char** key_path, int key_path_length);

/**
 * Return the next item in the found array
 * @param reader The json reader
 * @param item The item in the array, parsed into json structure, all array and nested items will be ignored currently
 * @return true if has next item, false if no next item, or the array hasn't been found
 */
bool
pgmoneta_json_next_array_item(struct json_reader* reader, struct json** item);

/**
 * Init a json object
 * @param item [out] The json item
 * @return 0 if success, 1 if otherwise
 */
int
pgmoneta_json_init(struct json** object);

/**
 * Free the json object
 * @param item The json object
 * @return 0 if success, 1 if otherwise
 */
int
pgmoneta_json_free(struct json* object);

/**
 * Put a key value pair into the json item,
 * if the key exists, value will be overwritten,
 *
 * If the kv pair is put into an empty json object, it will be treated as json item,
 * otherwise if the json object is an array, it will reject the kv pair
 * @param item The json item
 * @param key The json key
 * @param val The value data
 * @param type The value type
 * @return 0 on success, otherwise 1
 */
int
pgmoneta_json_put(struct json* item, char* key, uintptr_t val, enum value_type type);

/**
 * Append an entry into the json array
 * If the entry is put into an empty json object, it will be treated as json array,
 * otherwise if the json object is an item, it will reject the entry
 * @param array The json array
 * @param entry The entry data
 * @param type The entry value type
 * @return 0 is successful,
 * otherwise when the json object is an array, value is null, or value type conflicts with old value, 1 will be returned
 */
int
pgmoneta_json_append(struct json* array, uintptr_t entry, enum value_type type);

/**
 * Convert a json to string
 * @param object The json object
 * @param format The format
 * @param tag The optional tag
 * @param indent The indent
 * @return The json formatted string
 */
char*
pgmoneta_json_to_string(struct json* object, int32_t format, char* tag, int indent);

/**
 * Print a json object
 * @param object The object
 * @param format The format
 * @param indent_per_level The indent per level
 */
void
pgmoneta_json_print(struct json* object, int32_t format);

/**
 * Get json array length
 * @param array The json array
 * @return The length
 */
uint32_t
pgmoneta_json_array_length(struct json* array);

/**
 * Get the value data from json item
 * @param item The item
 * @param tag The tag
 * @return The value data, 0 if not found
 */
uintptr_t
pgmoneta_json_get(struct json* item, char* tag);

/**
 * Create a json iterator
 * @param object The JSON object
 * @param iter [out] The iterator
 * @return 0 on success, 1 if otherwise
 */
int
pgmoneta_json_iterator_init(struct json* object, struct json_iterator** iter);

/**
 * Destroy a iterator
 * @param iter The iterator
 */
void
pgmoneta_json_iterator_destroy(struct json_iterator* iter);

/**
 * Get the next kv pair/entry from JSON object
 * @param iter The iterator
 * @return true if has next, false if otherwise
 */
bool
pgmoneta_json_iterator_next(struct json_iterator* iter);

/**
 * Check if the JSON object has next kv pair/entry
 * @param iter The iterator
 * @return true if has next, false if otherwise
 */
bool
pgmoneta_json_iterator_has_next(struct json_iterator* iter);

/**
 * Parse a string into json item
 * @param str The string
 * @param obj [out] The json object
 * @return 0 if success, 1 if otherwise
 */
int
pgmoneta_json_parse_string(char* str, struct json** obj);

/**
 * Clone a json object
 * @param from The from object
 * @param to [out] The to object
 * @return 0 if success, 1 if otherwise
 */
int
pgmoneta_json_clone(struct json* from, struct json** to);

#ifdef __cplusplus
}
#endif

#endif
