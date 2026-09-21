#ifndef RK_LSP_JSON_H
#define RK_LSP_JSON_H

#include "../util.h"

typedef enum {
    JSON_NULL = 0,
    JSON_BOOL,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT
} JsonType;

typedef struct JsonVal JsonVal;

typedef struct JsonMember {
    char* key;
    JsonVal* val;
} JsonMember;

struct JsonVal {
    JsonType type;
    union {
        int bool_val;
        double num_val;
        char* str_val;
        struct {
            JsonVal** items;
            int count;
        } arr;
        struct {
            JsonMember* members;
            int count;
        } obj;
    } u;
};

/* Parsing */
JsonVal* json_parse(const char* src, int len);
void json_free(JsonVal* val);

/* Query helpers */
JsonVal* json_get(JsonVal* obj, const char* key);
const char* json_get_str(JsonVal* obj, const char* key);
int json_get_int(JsonVal* obj, const char* key, int default_val);
int json_get_bool(JsonVal* obj, const char* key, int default_val);
JsonVal* json_get_obj(JsonVal* obj, const char* key);
JsonVal* json_get_arr(JsonVal* obj, const char* key);

/* Serialization helpers using SB */
void json_emit_escaped_str(SB* sb, const char* s);
void json_emit_rpc_header(SB* sb, int content_len);

#endif
