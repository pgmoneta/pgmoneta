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

/* System */
#include <stdarg.h>

enum json_value_type {
   ValueInt8,
   ValueUInt8,
   ValueInt16,
   ValueUInt16,
   ValueInt32,
   ValueUInt32,
   ValueInt64,
   ValueUInt64,
   ValueBool,
   ValueString,
   ValueFloat,
   ValueObject,
   ValueInt8Array,
   ValueUInt8Array,
   ValueInt16Array,
   ValueUInt16Array,
   ValueInt32Array,
   ValueUInt32Array,
   ValueInt64Array,
   ValueUInt64Array,
   ValueStringArray,
   ValueFloatArray,
   ValueItemArray,
   ValueArrayArray,
};

enum json_type {
   JSONUnknown,
   JSONItem,
   JSONArray
};

// This is just a subset of all possible state during parsing,
// since we basically only look into the item array in json,
// some state transition will be fast-forwarded
enum json_reader_state {
   KeyStart,
   KeyEnd,
   ValueStart,
   ValueEnd,
   ArrayStart,
   ArrayEnd,
   ItemStart,
   ItemEnd,
   InvalidReaderState,
};

/** @struct json_value
 * Defines a JSON value
 */
struct json_value
{
   enum json_value_type type; /**< The json value type */
   uint32_t length;     /**< (optional) The json array length */
   // Payload can hold a 64bit value such as an integer, or be a pointer to a string, float or an array
   // Based on value type, payload could be converted to the following:
   // int64 (ValueInt), int64*(ValueInt64Array), char*(ValueString),
   // char**(ValueStringArray), float*(ValueFloat/ValueFloatArray),
   // json*(ValueObject), json**(ValueItemArray/ValueArrayArray)
   void* payload;             /**< The json value payload */
};

/** @struct json
 * Defines a JSON structure
 */
struct json
{
   // a json object can only be item or array
   enum json_type type;          /**< The json object type */
   // if the object is an array, it can have at most one json element
   struct json_element* element;   /**< The kv element start */
};

/** @struct json_element
 * Defines a JSON element
 */
struct json_element
{
   // if the element holds an array value,
   // if it belongs to a json array object, then the key is NULL, and it will not have next element,
   // if it belongs to a json item object, then the key is not NULL, and it probably has next element
   char* key;                    /**< The json element key */
   struct json_value* value;     /**< The json element value */
   struct json_element* next;    /**< The next element */
};

/** @struct json_reader
 * Defines a JSON reader
 */
struct json_reader
{
   struct stream_buffer* buffer; /**< The buffer */
   int fd;                       /**< The file descriptor */
   enum json_reader_state state; /**< The current reader state of the JSON reader */
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
 * Parse input to a json item as the reader progresses through the input stream,
 * reader should be in ItemStart state.
 * The function is the non-recursive version - it will ignore array and nested item,
 * since we don't need that for now.
 * @param reader The reader
 * @param item [out]The item
 * @return 0 on success, 1 if otherwise
 */
int
pgmoneta_json_stream_parse_item(struct json_reader* reader, struct json** item);

/**
 * Get int8_t value from json
 * @param item The item
 * @param key The key
 * @return The value, 0 if not found
 */
int8_t
pgmoneta_json_get_int8(struct json* item, char* key);

/**
 * Get uint8_t value from json
 * @param item The item
 * @param key The key
 * @return The value, 0 if not found
 */
uint8_t
pgmoneta_json_get_uint8(struct json* item, char* key);

/**
 * Get int16_t value from json
 * @param item The item
 * @param key The key
 * @return The value, 0 if not found
 */
int16_t
pgmoneta_json_get_int16(struct json* item, char* key);

/**
 * Get uint16_t value from json
 * @param item The item
 * @param key The key
 * @return The value, 0 if not found
 */
uint16_t
pgmoneta_json_get_uint16(struct json* item, char* key);

/**
 * Get int32_t value from json
 * @param item The item
 * @param key The key
 * @return The value, 0 if not found
 */
int32_t
pgmoneta_json_get_int32(struct json* item, char* key);

/**
 * Get uint32_t value from json
 * @param item The item
 * @param key The key
 * @return The value, 0 if not found
 */
uint32_t
pgmoneta_json_get_uint32(struct json* item, char* key);

/**
 * Get int64_t value from json
 * @param item The item
 * @param key The key
 * @return The value, 0 if not found
 */
int64_t
pgmoneta_json_get_int64(struct json* item, char* key);

/**
 * Get uint64 value from json
 * @param item The item
 * @param key The key
 * @return The value, 0 if not found
 */
uint64_t
pgmoneta_json_get_uint64(struct json* item, char* key);

/**
 * Get bool value from json
 * @param item The item
 * @param key The key
 * @return The value, 0 if not found
 */
bool
pgmoneta_json_get_bool(struct json* item, char* key);

/**
 * Get int8 array value from json
 * @param item The item
 * @param key The key
 * @return The value, NULL if not found
 */
int8_t*
pgmoneta_json_get_int8_array(struct json* item, char* key);

/**
 * Get uint8 array value from json
 * @param item The item
 * @param key The key
 * @return The value, NULL if not found
 */
uint8_t*
pgmoneta_json_get_uint8_array(struct json* item, char* key);

/**
 * Get int16 array value from json
 * @param item The item
 * @param key The key
 * @return The value, NULL if not found
 */
int16_t*
pgmoneta_json_get_int16_array(struct json* item, char* key);

/**
 * Get uint16 array value from json
 * @param item The item
 * @param key The key
 * @return The value, NULL if not found
 */
uint16_t*
pgmoneta_json_get_uint16_array(struct json* item, char* key);

/**
 * Get int32 array value from json
 * @param item The item
 * @param key The key
 * @return The value, NULL if not found
 */
int32_t*
pgmoneta_json_get_int32_array(struct json* item, char* key);

/**
 * Get uint32 array value from json
 * @param item The item
 * @param key The key
 * @return The value, NULL if not found
 */
uint32_t*
pgmoneta_json_get_uint32_array(struct json* item, char* key);

/**
 * Get int64 array value from json
 * @param item The item
 * @param key The key
 * @return The value, NULL if not found
 */
int64_t*
pgmoneta_json_get_int64_array(struct json* item, char* key);

/**
 * Get uint64 array value from json
 * @param item The item
 * @param key The key
 * @return The value, NULL if not found
 */
uint64_t*
pgmoneta_json_get_uint64_array(struct json* item, char* key);

/**
 * Get string value from json
 * @param item The item
 * @param key The key
 * @return The value, NULL if not found
 */
char*
pgmoneta_json_get_string(struct json* item, char* key);

/**
 * Get string array value from json
 * @param item The item
 * @param key The key
 * @return The value, NULL if not found
 */
char**
pgmoneta_json_get_string_array(struct json* item, char* key);

/**
 * Get float value from json
 * @param item The item
 * @param key The key
 * @return The value, 0 if not found
 */
float
pgmoneta_json_get_float(struct json* item, char* key);

/**
 * Get float array value from json
 * @param item The item
 * @param key The key
 * @return The value, NULL if not found
 */
float*
pgmoneta_json_get_float_array(struct json* item, char* key);

/**
 * Get json object value from json.
 * @param item The item
 * @param key The key
 * @return The value, NULL if not found
 */
struct json*
pgmoneta_json_get_json_object(struct json* item, char* key);

/**
 * Get json object array value from json
 * @param item The item
 * @param key The key
 * @return The value, NULL if not found
 */
struct json**
pgmoneta_json_get_json_object_array(struct json* item, char* key);

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
 * if value is of primitive types, old value will be freed if necessary
 *
 * If the kv pair is put into an empty json object, it will be treated as json item,
 * otherwise if the json object is an array, it will reject the kv pair
 * @param item The json item
 * @param key The key, which will be copied into the item
 * @param val The pointer to the value, which will be interpreted differently based on the value type
 * @param type The json value type
 * @param length [Maybe unused] The length of the value if it is an array, necessary when the value is array
 * @return 0 is successful,
 * otherwise when the json object is an array, value is null, or value type conflicts with old value, 1 will be returned
 */
int
pgmoneta_json_put(struct json* item, char* key, void* val, enum json_value_type type, ...);

/**
 * Append an entry into the json array
 * If the entry is put into an empty json object, it will be treated as json array,
 * otherwise if the json object is an item, it will reject the entry
 * @param entry The pointer to the entry, which will be interpreted differently based on the type
 * @param type The json value type
 * @return 0 is successful,
 * otherwise when the json object is an array, value is null, or value type conflicts with old value, 1 will be returned
 */
int
pgmoneta_json_append(struct json* array, void* entry, enum json_value_type type);

/**
 * Print a json object
 * @param object The object
 * @param indent_per_level The indent per level
 */
void
pgmoneta_json_print(struct json* object, int indent_per_level);

/**
 * Get the json element from json array
 * @param index The index
 * @return The element, NULL if error or index out of bound;
 */
struct json*
pgmoneta_json_array_get(struct json* array, int index);

/**
 * Get json array length
 * @param array The json array
 * @return The length
 */
uint32_t
pgmoneta_json_array_length(struct json* array);

#ifdef __cplusplus
}
#endif

#endif
