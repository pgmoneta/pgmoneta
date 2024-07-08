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
/**
 * JSON related command tags, used to build and retrieve
 * a JSON piece of information related to a single command
 */
#define JSON_TAG_COMMAND "command"
#define JSON_TAG_COMMAND_NAME "name"
#define JSON_TAG_COMMAND_STATUS "status"
#define JSON_TAG_COMMAND_ERROR "error"
#define JSON_TAG_COMMAND_OUTPUT "output"
#define JSON_TAG_COMMAND_EXIT_STATUS "exit-status"

#define JSON_TAG_APPLICATION_NAME "name"
#define JSON_TAG_APPLICATION_VERSION_MAJOR "major"
#define JSON_TAG_APPLICATION_VERSION_MINOR "minor"
#define JSON_TAG_APPLICATION_VERSION_PATCH "patch"
#define JSON_TAG_APPLICATION_VERSION "version"

#define JSON_TAG_ARRAY_NAME "list"

/**
 * JSON pre-defined values
 */
#define JSON_STRING_SUCCESS "OK"
#define JSON_STRING_ERROR   "KO"
#define JSON_BOOL_SUCCESS   0
#define JSON_BOOL_ERROR     1

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
   unsigned short length;     /**< (optional) The json array length */
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
 * Read a new chunk of json file into memory
 * @param reader The reader
 * @return 0 if success, 1 if otherwise
 */
int
pgmoneta_json_read(struct json_reader* reader);

/**
 * Get the next char from json file
 * @param reader The reader
 * @param next [out] The next char
 * @return true if has next, false if otherwise
 */
bool
pgmoneta_json_next_char(struct json_reader* reader, char* next);

/**
 * Get the next char without forwarding the cursor
 * @param reader The reader
 * @param next [out] The next char
 * @return true if has next, false if otherwise
 */
bool
pgmoneta_json_peek_next_char(struct json_reader* reader, char* next);

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
pgmoneta_json_close_reader(struct json_reader* reader);

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
 * Fast-forward until current value ends
 * @param reader The reader
 * @param ch Current character reader is at
 * @return 0 if success, 1 if otherwise
 */
int
pgmoneta_json_fast_forward_value(struct json_reader* reader, char ch);

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
 * Given the key and the json item, find the value
 * @param item The json item
 * @param key The key
 * @return the json value, null if not found
 */
struct json_value*
pgmoneta_json_get_value(struct json* item, char* key);

/**
 * Get int8_t value from json
 * @param item The item
 * @param key The key
 * @return The value, 0 if not found
 */
int8_t
pgmoneta_json_get_int8_value(struct json* item, char* key);

/**
 * Get uint8_t value from json
 * @param item The item
 * @param key The key
 * @return The value, 0 if not found
 */
uint8_t
pgmoneta_json_get_uint8_value(struct json* item, char* key);

/**
 * Get int16_t value from json
 * @param item The item
 * @param key The key
 * @return The value, 0 if not found
 */
int16_t
pgmoneta_json_get_int16_value(struct json* item, char* key);

/**
 * Get uint16_t value from json
 * @param item The item
 * @param key The key
 * @return The value, 0 if not found
 */
uint16_t
pgmoneta_json_get_uint16_value(struct json* item, char* key);

/**
 * Get int32_t value from json
 * @param item The item
 * @param key The key
 * @return The value, 0 if not found
 */
int32_t
pgmoneta_json_get_int32_value(struct json* item, char* key);

/**
 * Get uint32_t value from json
 * @param item The item
 * @param key The key
 * @return The value, 0 if not found
 */
uint32_t
pgmoneta_json_get_uint32_value(struct json* item, char* key);

/**
 * Get int64_t value from json
 * @param item The item
 * @param key The key
 * @return The value, 0 if not found
 */
int64_t
pgmoneta_json_get_int64_value(struct json* item, char* key);

/**
 * Get uint64 value from json
 * @param item The item
 * @param key The key
 * @return The value, 0 if not found
 */
uint64_t
pgmoneta_json_get_uint64_value(struct json* item, char* key);

/**
 * Get bool value from json
 * @param item The item
 * @param key The key
 * @return The value, 0 if not found
 */
bool
pgmoneta_json_get_bool_value(struct json* item, char* key);

/**
 * Get int8 array value from json
 * @param item The item
 * @param key The key
 * @return The value, NULL if not found
 */
int8_t*
pgmoneta_json_get_int8_array_value(struct json* item, char* key);

/**
 * Get uint8 array value from json
 * @param item The item
 * @param key The key
 * @return The value, NULL if not found
 */
uint8_t*
pgmoneta_json_get_uint8_array_value(struct json* item, char* key);

/**
 * Get int16 array value from json
 * @param item The item
 * @param key The key
 * @return The value, NULL if not found
 */
int16_t*
pgmoneta_json_get_int16_array_value(struct json* item, char* key);

/**
 * Get uint16 array value from json
 * @param item The item
 * @param key The key
 * @return The value, NULL if not found
 */
uint16_t*
pgmoneta_json_get_uint16_array_value(struct json* item, char* key);

/**
 * Get int32 array value from json
 * @param item The item
 * @param key The key
 * @return The value, NULL if not found
 */
int32_t*
pgmoneta_json_get_int32_array_value(struct json* item, char* key);

/**
 * Get uint32 array value from json
 * @param item The item
 * @param key The key
 * @return The value, NULL if not found
 */
uint32_t*
pgmoneta_json_get_uint32_array_value(struct json* item, char* key);

/**
 * Get int64 array value from json
 * @param item The item
 * @param key The key
 * @return The value, NULL if not found
 */
int64_t*
pgmoneta_json_get_int64_array_value(struct json* item, char* key);

/**
 * Get uint64 array value from json
 * @param item The item
 * @param key The key
 * @return The value, NULL if not found
 */
uint64_t*
pgmoneta_json_get_uint64_array_value(struct json* item, char* key);

/**
 * Get string value from json
 * @param item The item
 * @param key The key
 * @return The value, NULL if not found
 */
char*
pgmoneta_json_get_string_value(struct json* item, char* key);

/**
 * Get string array value from json
 * @param item The item
 * @param key The key
 * @return The value, NULL if not found
 */
char**
pgmoneta_json_get_string_array_value(struct json* item, char* key);

/**
 * Get float value from json
 * @param item The item
 * @param key The key
 * @return The value, 0 if not found
 */
float
pgmoneta_json_get_float_value(struct json* item, char* key);

/**
 * Get float array value from json
 * @param item The item
 * @param key The key
 * @return The value, NULL if not found
 */
float*
pgmoneta_json_get_float_array_value(struct json* item, char* key);

/**
 * Get json object value from json.
 * @param item The item
 * @param key The key
 * @return The value, NULL if not found
 */
struct json*
pgmoneta_json_get_json_object_value(struct json* item, char* key);

/**
 * Get json object array value from json
 * @param item The item
 * @param key The key
 * @return The value, NULL if not found
 */
struct json**
pgmoneta_json_get_json_object_array_value(struct json* item, char* key);

/**
 * Init a json object
 * @param item [out] The json item
 * @param type The type
 * @return 0 if success, 1 if otherwise
 */
int
pgmoneta_json_init(struct json** object, enum json_type type);

/**
 * Free the json object
 * @param item The json object
 * @return 0 if success, 1 if otherwise
 */
int
pgmoneta_json_free(struct json* object);

/**
 * Init the json element
 * @param value [out] The value
 * @return 0 if success, 1 if otherwise
 */
int
pgmoneta_json_element_init(struct json_element** element);

/**
 * Free the json element
 * @param value The value
 * @return The next json element
 */
struct json_element*
pgmoneta_json_element_free(struct json_element* element);

/**
 * Init the json value
 * @param value [out] The value
 * @param type The type
 * @return 0 if success, 1 if otherwise
 */
int
pgmoneta_json_value_init(struct json_value** value, enum json_value_type type);

/**
 * Free the json value
 * @param value The value
 * @return 0 if success, 1 if otherwise
 */
int
pgmoneta_json_value_free(struct json_value* value);

/**
 * Append value to array typed json value
 * If array is int/float/string array, value will be read, copied and appended to the array payload points to.
 * If array is array/item array, payload points to an array of json pointer,
 * ptr will be treated as a json pointer and appended to this array.
 * This function does not provide type check
 * @param value The json value
 * @param ptr The pointer
 * @return 0 if success, 1 if otherwise
 */
int
pgmoneta_json_value_append(struct json_value* value, void* ptr);

/**
 * Wrapper of pgmoneta_json_value_append, it does not run type check
 * @param array The json array
 * @param ptr The payload
 * @return 0 if success, 1 if otherwise
 */
int
pgmoneta_json_array_append(struct json* array, void* ptr);

/**
 * Add an element to the json object, either array or item.
 * Does not check for duplication
 * Returns error when the object is json array and already has an element
 * @param object The json object
 * @param element The element
 * @return 0 if success, 1 if otherwise
 */
int
pgmoneta_json_add(struct json* object, struct json_element* element);

/**
 * Put a key value pair to a json object, the key will be copied, while the payload will not
 * @param object The json object
 * @param key The key
 * @param type The value type
 * @param payload The value payload
 * @param length The array typed value length, unused if value is not array
 * @return 0 if success, 1 if otherwise
 */
int
pgmoneta_json_object_put(struct json* object, char* key, enum json_value_type type, void* payload, unsigned short length);

/**
 * Put a string kv pair to json item, the key and string will be copied.
 * This function frees the old string value and replace with new one, if the key exists.
 * @param item The item
 * @param key The key
 * @param val The string value
 * @return 0 if success, 1 if otherwise
 */
int
pgmoneta_json_item_put_string(struct json* item, char* key, char* val);

/**
 * Put an int8 kv pair to json item, the key will be copied.
 * This function frees the old int8 value and replace with new one, if the key exists.
 * @param item The item
 * @param key The key
 * @param val The int8 value
 * @return 0 if success, 1 if otherwise
 */
int
pgmoneta_json_item_put_int8(struct json* item, char* key, int8_t val);

/**
 * Put an uint8 kv pair to json item, the key will be copied.
 * This function frees the old uint8 value and replace with new one, if the key exists.
 * @param item The item
 * @param key The key
 * @param val The uint8 value
 * @return 0 if success, 1 if otherwise
 */
int
pgmoneta_json_item_put_uint8(struct json* item, char* key, uint8_t val);

/**
 * Put an int16 kv pair to json item, the key will be copied.
 * This function frees the old int16 value and replace with new one, if the key exists.
 * @param item The item
 * @param key The key
 * @param val The int16 value
 * @return 0 if success, 1 if otherwise
 */
int
pgmoneta_json_item_put_int16(struct json* item, char* key, int16_t val);

/**
 * Put an uint16 kv pair to json item, the key will be copied.
 * This function frees the old uint16 value and replace with new one, if the key exists.
 * @param item The item
 * @param key The key
 * @param val The uint16 value
 * @return 0 if success, 1 if otherwise
 */
int
pgmoneta_json_item_put_uint16(struct json* item, char* key, uint16_t val);

/**
 * Put an int32 kv pair to json item, the key will be copied.
 * This function frees the old int32 value and replace with new one, if the key exists.
 * @param item The item
 * @param key The key
 * @param val The int32 value
 * @return 0 if success, 1 if otherwise
 */
int
pgmoneta_json_item_put_int32(struct json* item, char* key, int32_t val);

/**
 * Put an uint32 kv pair to json item, the key will be copied.
 * This function frees the old uint32 value and replace with new one, if the key exists.
 * @param item The item
 * @param key The key
 * @param val The uint32 value
 * @return 0 if success, 1 if otherwise
 */
int
pgmoneta_json_item_put_uint32(struct json* item, char* key, uint32_t val);

/**
 * Put an int64 kv pair to json item, the key will be copied.
 * This function frees the old int64 value and replace with new one, if the key exists.
 * @param item The item
 * @param key The key
 * @param val The int64 value
 * @return 0 if success, 1 if otherwise
 */
int
pgmoneta_json_item_put_int64(struct json* item, char* key, int64_t val);

/**
 * Put an uint64 kv pair to json item, the key will be copied.
 * This function frees the old uint64 value and replace with new one, if the key exists.
 * @param item The item
 * @param key The key
 * @param val The uint64 value
 * @return 0 if success, 1 if otherwise
 */
int
pgmoneta_json_item_put_uint64(struct json* item, char* key, uint64_t val);

/**
 * Put an bool kv pair to json item, the key will be copied.
 * This function frees the old bool value and replace with new one, if the key exists.
 * @param item The item
 * @param key The key
 * @param val The bool value
 * @return 0 if success, 1 if otherwise
 */
int
pgmoneta_json_item_put_bool(struct json* item, char* key, bool val);

/**
 * Put a float kv pair to json item, the key will be copied
 * @param item The item
 * @param key The key
 * @param val The float value
 * @return 0 if success, 1 if otherwise
 */
int
pgmoneta_json_item_put_float(struct json* item, char* key, float val);

/**
 * Put an json object kv pair to json item, the key will be copied but the object is not.
 * The object could be an array or an item.
 * @param item The item
 * @param key The key
 * @param val The item value
 * @return 0 if success, 1 if otherwise
 */
int
pgmoneta_json_item_put_object(struct json* item, char* key, struct json* val);

/**
 * Put an int8 array to json item, it copies the key, but not the array
 * @param item The item
 * @param key The key
 * @param vals The int8 array
 * @param length The array length
 * @return 1 if success, otherwise 0
 */
int
pgmoneta_json_item_put_int8_array(struct json* item, char* key, int8_t* vals, unsigned short length);

/**
 * Put an uint8 array to json item, it copies the key, but not the array
 * @param item The item
 * @param key The key
 * @param vals The uint8 array
 * @param length The array length
 * @return 1 if success, otherwise 0
 */

int
pgmoneta_json_item_put_uint8_array(struct json* item, char* key, uint8_t* vals, unsigned short length);
/**
 * Put an int16 array to json item, it copies the key, but not the array
 * @param item The item
 * @param key The key
 * @param vals The int16 array
 * @param length The array length
 * @return 1 if success, otherwise 0
 */
int
pgmoneta_json_item_put_int16_array(struct json* item, char* key, int16_t* vals, unsigned short length);

/**
 * Put an uint16 array to json item, it copies the key, but not the array
 * @param item The item
 * @param key The key
 * @param vals The uint16 array
 * @param length The array length
 * @return 1 if success, otherwise 0
 */
int
pgmoneta_json_item_put_uint16_array(struct json* item, char* key, uint16_t* vals, unsigned short length);

/**
 * Put an int32 array to json item, it copies the key, but not the array
 * @param item The item
 * @param key The key
 * @param vals The int32 array
 * @param length The array length
 * @return 1 if success, otherwise 0
 */
int
pgmoneta_json_item_put_int32_array(struct json* item, char* key, int32_t* vals, unsigned short length);

/**
 * Put an uint32 array to json item, it copies the key, but not the array
 * @param item The item
 * @param key The key
 * @param vals The uint32 array
 * @param length The array length
 * @return 1 if success, otherwise 0
 */
int
pgmoneta_json_item_put_uint32_array(struct json* item, char* key, uint32_t* vals, unsigned short length);

/**
 * Put an int64 array to json item, it copies the key, but not the array
 * @param item The item
 * @param key The key
 * @param vals The int64 array
 * @param length The array length
 * @return 1 if success, otherwise 0
 */
int
pgmoneta_json_item_put_int64_array(struct json* item, char* key, int64_t* vals, unsigned short length);

/**
 * Put an uint64 array to json item, it copies the key, but not the array
 * @param item The item
 * @param key The key
 * @param vals The uint64 array
 * @param length The array length
 * @return 1 if success, otherwise 0
 */
int
pgmoneta_json_item_put_uint64_array(struct json* item, char* key, uint64_t* vals, unsigned short length);

/**
 * Put a json item array to json item, it copies the key, but not the array
 * @param item The item
 * @param key The key
 * @param items The json item array
 * @param length The array length
 * @return 1 if success, otherwise 0
 */
int
pgmoneta_json_item_put_item_array(struct json* item, char* key, struct json* items, unsigned short length);

/**
 * Add an int8 value to the json array,
 * method will be denied if the type does not match
 * @param array The array
 * @param val The value
 * @return 1 if success, otherwise 0
 */
int
pgmoneta_json_array_append_int8(struct json* array, int8_t val);

/**
 * Add an uint8 value to the json array,
 * method will be denied if the type does not match
 * @param array The array
 * @param val The value
 * @return 1 if success, otherwise 0
 */
int
pgmoneta_json_array_append_uint8(struct json* array, uint8_t val);

/**
 * Add an int16 value to the json array,
 * method will be denied if the type does not match
 * @param array The array
 * @param val The value
 * @return 1 if success, otherwise 0
 */
int
pgmoneta_json_array_append_int16(struct json* array, int16_t val);

/**
 * Add an uint16 value to the json array,
 * method will be denied if the type does not match
 * @param array The array
 * @param val The value
 * @return 1 if success, otherwise 0
 */
int
pgmoneta_json_array_append_uint16(struct json* array, uint16_t val);

/**
 * Add an int32 value to the json array,
 * method will be denied if the type does not match
 * @param array The array
 * @param val The value
 * @return 1 if success, otherwise 0
 */
int
pgmoneta_json_array_append_int32(struct json* array, int32_t val);

/**
 * Add an uint32 value to the json array,
 * method will be denied if the type does not match
 * @param array The array
 * @param val The value
 * @return 1 if success, otherwise 0
 */
int
pgmoneta_json_array_append_uint32(struct json* array, uint32_t val);

/**
 * Add an int64 value to the json array,
 * method will be denied if the type does not match
 * @param array The array
 * @param val The value
 * @return 1 if success, otherwise 0
 */
int
pgmoneta_json_array_append_int64(struct json* array, int64_t val);

/**
 * Add an uint64 value to the json array,
 * method will be denied if the type does not match
 * @param array The array
 * @param val The value
 * @return 1 if success, otherwise 0
 */
int
pgmoneta_json_array_append_uint64(struct json* array, uint64_t val);

/**
 * Put a string to the json array, the string will be copied,
 * method will be denied if the type does not match
 * @param array The array
 * @param val The string
 * @return 1 if success, otherwise 0
 */
int
pgmoneta_json_array_append_string(struct json* array, char* str);

/**
 * Put an object to the json array, the item is not copied
 * method will be denied if the type does not match
 * @param array The array
 * @param object The json object
 * @return 1 if success, otherwise 0
 */
int
pgmoneta_json_array_append_object(struct json* array, struct json* object);

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
unsigned short
pgmoneta_json_array_length(struct json* array);

/**
 * Reset the json reader to where json file starts
 * @return 0 if success, 1 if otherwise
 */
int
pgmoneta_json_reader_reset(struct json_reader* reader);

/**
 * Utility method to create a new JSON object that wraps a
 * single command. This method should be called to initialize the
 * object and then the other specific methods that read the
 * answer from pgmoneta should populate the object accordingly.
 *
 * Moreover, an 'application' object is placed to indicate from
 * where the command has been launched (i.e., which executable)
 * and at which version.
 *
 * @param command_name the name of the command this object wraps
 * an answer for
 * @param success true if the command is supposed to be successful
 * @returns the new JSON object to use and populate
 * @param executable_name the name of the executable that is creating this
 * response object
 */
struct json*
pgmoneta_json_create_new_command_object(char* command_name, bool success, char* executable_name);

/**
 * Utility method to "jump" to the output JSON object wrapped into
 * a command object.
 *
 * The "output" object is the one that every single method that reads
 * back an answer from pgmoneta has to populate in a specific
 * way according to the data received from pgmoneta.
 *
 * @param json the command object that wraps the command
 * @returns the pointer to the output object of NULL in case of an error
 */
struct json*
pgmoneta_json_extract_command_output_object(struct json* json);

/**
 * Utility function to set a command JSON object as faulty, that
 * means setting the 'error' and 'status' message accordingly.
 *
 * @param json the whole json object that must include the 'command'
 * tag
 * @param message the message to use to set the faulty diagnostic
 * indication
 *
 * @param exit status
 *
 * @returns 0 on success
 *
 * Example:
 * json_set_command_object_faulty( json, strerror( errno ) );
 */
int
pgmoneta_json_set_command_object_faulty(struct json* json, char* message);

/**
 * Utility method to inspect if a JSON object that wraps a command
 * is faulty, that means if it has the error flag set to true.
 *
 * @param json the json object to analyzer
 * @returns the value of the error flag in the object, or false if
 * the object is not valid
 */
bool
pgmoneta_json_is_command_object_faulty(struct json* json);

/**
 * Utility method to extract the message related to the status
 * of the command wrapped in the JSON object.
 *
 * @param json the JSON object to analyze
 * #returns the status message or NULL in case the JSON object is not valid
 */
const char*
pgmoneta_json_get_command_object_status(struct json* json);

/**
 * Utility method to check if a JSON object wraps a specific command name.
 *
 * @param json the JSON object to analyze
 * @param command_name the name to search for
 * @returns true if the command name matches, false otherwise and in case
 * the JSON object is not valid or the command name is not valid
 */
bool
pgmoneta_json_command_name_equals_to(struct json* json, char* command_name);

/**
 * Utility method to print out the JSON object
 * on standard output.
 *
 * After the object has been printed, it is destroyed, so
 * calling this method will make the pointer invalid
 * and the jeon object cannot be used anymore.
 *
 * This should be the last method to be called
 * when there is the need to print out the information
 * contained in a json object.
 *
 * @param json the json object to print
 */
void
pgmoneta_json_print_and_free_json_object(struct json* json);

#ifdef __cplusplus
}
#endif

#endif
