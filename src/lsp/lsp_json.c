#include "lsp_json.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

typedef struct {
    const char* src;
    int len;
    int pos;
} Parser;

static void skip_ws(Parser* p) {
    while (p->pos < p->len) {
        char c = p->src[p->pos];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            p->pos++;
        } else {
            break;
        }
    }
}

static JsonVal* parse_val(Parser* p);

static char* parse_str_raw(Parser* p) {
    if (p->pos >= p->len || p->src[p->pos] != '"') return NULL;
    p->pos++; /* skip open quote */
    SB sb;
    sb_init(&sb);
    while (p->pos < p->len) {
        char c = p->src[p->pos++];
        if (c == '"') {
            char* res = sb_strdup(&sb);
            sb_free(&sb);
            return res ? res : strdup("");
        }
        if (c == '\\' && p->pos < p->len) {
            char esc = p->src[p->pos++];
            switch (esc) {
            case '"': sb_appendn(&sb, "\"", 1); break;
            case '\\': sb_appendn(&sb, "\\", 1); break;
            case '/': sb_appendn(&sb, "/", 1); break;
            case 'b': sb_appendn(&sb, "\b", 1); break;
            case 'f': sb_appendn(&sb, "\f", 1); break;
            case 'n': sb_appendn(&sb, "\n", 1); break;
            case 'r': sb_appendn(&sb, "\r", 1); break;
            case 't': sb_appendn(&sb, "\t", 1); break;
            case 'u': {
                /* 4-hex unicode digits: handle simple ascii range */
                int codepoint = 0;
                for (int i = 0; i < 4 && p->pos < p->len; i++) {
                    char h = p->src[p->pos++];
                    codepoint <<= 4;
                    if (h >= '0' && h <= '9') codepoint |= (h - '0');
                    else if (h >= 'a' && h <= 'f') codepoint |= (h - 'a' + 10);
                    else if (h >= 'A' && h <= 'F') codepoint |= (h - 'A' + 10);
                }
                if (codepoint > 0 && codepoint <= 0x7F) {
                    char ch = (char)codepoint;
                    sb_appendn(&sb, &ch, 1);
                } else {
                    sb_appendn(&sb, "?", 1);
                }
                break;
            }
            default: sb_appendn(&sb, &esc, 1); break;
            }
        } else {
            sb_appendn(&sb, &c, 1);
        }
    }
    sb_free(&sb);
    return NULL;
}

static JsonVal* parse_obj(Parser* p) {
    if (p->pos >= p->len || p->src[p->pos] != '{') return NULL;
    p->pos++; /* skip '{' */

    JsonVal* v = calloc(1, sizeof *v);
    v->type = JSON_OBJECT;

    for (;;) {
        skip_ws(p);
        if (p->pos >= p->len) break;
        if (p->src[p->pos] == '}') {
            p->pos++;
            return v;
        }

        char* key = parse_str_raw(p);
        if (!key) break;

        skip_ws(p);
        if (p->pos < p->len && p->src[p->pos] == ':') {
            p->pos++;
        } else {
            free(key);
            break;
        }

        JsonVal* child = parse_val(p);
        if (!child) {
            free(key);
            break;
        }

        v->u.obj.members = realloc(v->u.obj.members, (v->u.obj.count + 1) * sizeof(JsonMember));
        v->u.obj.members[v->u.obj.count].key = key;
        v->u.obj.members[v->u.obj.count].val = child;
        v->u.obj.count++;

        skip_ws(p);
        if (p->pos < p->len && p->src[p->pos] == ',') {
            p->pos++;
            continue;
        } else if (p->pos < p->len && p->src[p->pos] == '}') {
            p->pos++;
            return v;
        }
        break;
    }
    return v;
}

static JsonVal* parse_arr(Parser* p) {
    if (p->pos >= p->len || p->src[p->pos] != '[') return NULL;
    p->pos++; /* skip '[' */

    JsonVal* v = calloc(1, sizeof *v);
    v->type = JSON_ARRAY;

    for (;;) {
        skip_ws(p);
        if (p->pos >= p->len) break;
        if (p->src[p->pos] == ']') {
            p->pos++;
            return v;
        }

        JsonVal* child = parse_val(p);
        if (!child) break;

        v->u.arr.items = realloc(v->u.arr.items, (v->u.arr.count + 1) * sizeof(JsonVal*));
        v->u.arr.items[v->u.arr.count++] = child;

        skip_ws(p);
        if (p->pos < p->len && p->src[p->pos] == ',') {
            p->pos++;
            continue;
        } else if (p->pos < p->len && p->src[p->pos] == ']') {
            p->pos++;
            return v;
        }
        break;
    }
    return v;
}

static JsonVal* parse_val(Parser* p) {
    skip_ws(p);
    if (p->pos >= p->len) return NULL;
    char c = p->src[p->pos];

    if (c == '{') return parse_obj(p);
    if (c == '[') return parse_arr(p);

    if (c == '"') {
        char* s = parse_str_raw(p);
        if (!s) return NULL;
        JsonVal* v = calloc(1, sizeof *v);
        v->type = JSON_STRING;
        v->u.str_val = s;
        return v;
    }

    if (c == 't' && p->pos + 4 <= p->len && memcmp(p->src + p->pos, "true", 4) == 0) {
        p->pos += 4;
        JsonVal* v = calloc(1, sizeof *v);
        v->type = JSON_BOOL;
        v->u.bool_val = 1;
        return v;
    }

    if (c == 'f' && p->pos + 5 <= p->len && memcmp(p->src + p->pos, "false", 5) == 0) {
        p->pos += 5;
        JsonVal* v = calloc(1, sizeof *v);
        v->type = JSON_BOOL;
        v->u.bool_val = 0;
        return v;
    }

    if (c == 'n' && p->pos + 4 <= p->len && memcmp(p->src + p->pos, "null", 4) == 0) {
        p->pos += 4;
        JsonVal* v = calloc(1, sizeof *v);
        v->type = JSON_NULL;
        return v;
    }

    if (c == '-' || (c >= '0' && c <= '9')) {
        int start = p->pos;
        if (c == '-') p->pos++;
        while (p->pos < p->len && ((p->src[p->pos] >= '0' && p->src[p->pos] <= '9') || p->src[p->pos] == '.' || p->src[p->pos] == 'e' || p->src[p->pos] == 'E' || p->src[p->pos] == '+' || p->src[p->pos] == '-')) {
            p->pos++;
        }
        int num_len = p->pos - start;
        char buf[64];
        if (num_len >= (int)sizeof(buf)) num_len = sizeof(buf) - 1;
        memcpy(buf, p->src + start, num_len);
        buf[num_len] = '\0';
        JsonVal* v = calloc(1, sizeof *v);
        v->type = JSON_NUMBER;
        v->u.num_val = atof(buf);
        return v;
    }

    return NULL;
}

JsonVal* json_parse(const char* src, int len) {
    if (!src || len <= 0) return NULL;
    Parser p = { .src = src, .len = len, .pos = 0 };
    return parse_val(&p);
}

void json_free(JsonVal* val) {
    if (!val) return;
    if (val->type == JSON_STRING) {
        free(val->u.str_val);
    } else if (val->type == JSON_ARRAY) {
        for (int i = 0; i < val->u.arr.count; i++) {
            json_free(val->u.arr.items[i]);
        }
        free(val->u.arr.items);
    } else if (val->type == JSON_OBJECT) {
        for (int i = 0; i < val->u.obj.count; i++) {
            free(val->u.obj.members[i].key);
            json_free(val->u.obj.members[i].val);
        }
        free(val->u.obj.members);
    }
    free(val);
}

JsonVal* json_get(JsonVal* obj, const char* key) {
    if (!obj || obj->type != JSON_OBJECT || !key) return NULL;
    for (int i = 0; i < obj->u.obj.count; i++) {
        if (strcmp(obj->u.obj.members[i].key, key) == 0) {
            return obj->u.obj.members[i].val;
        }
    }
    return NULL;
}

const char* json_get_str(JsonVal* obj, const char* key) {
    JsonVal* v = json_get(obj, key);
    if (v && v->type == JSON_STRING) return v->u.str_val;
    return NULL;
}

int json_get_int(JsonVal* obj, const char* key, int default_val) {
    JsonVal* v = json_get(obj, key);
    if (v && v->type == JSON_NUMBER) return (int)v->u.num_val;
    return default_val;
}

int json_get_bool(JsonVal* obj, const char* key, int default_val) {
    JsonVal* v = json_get(obj, key);
    if (v && v->type == JSON_BOOL) return v->u.bool_val;
    return default_val;
}

JsonVal* json_get_obj(JsonVal* obj, const char* key) {
    JsonVal* v = json_get(obj, key);
    if (v && v->type == JSON_OBJECT) return v;
    return NULL;
}

JsonVal* json_get_arr(JsonVal* obj, const char* key) {
    JsonVal* v = json_get(obj, key);
    if (v && v->type == JSON_ARRAY) return v;
    return NULL;
}

void json_emit_escaped_str(SB* sb, const char* s) {
    sb_append(sb, "\"");
    for (const char* p = s ? s : ""; *p; p++) {
        unsigned char c = (unsigned char)*p;
        char esc[7];
        int n = 0;
        if (c == '"') { esc[0]='\\'; esc[1]='"'; n=2; }
        else if (c == '\\') { esc[0]='\\'; esc[1]='\\'; n=2; }
        else if (c == '\n') { esc[0]='\\'; esc[1]='n'; n=2; }
        else if (c == '\r') { esc[0]='\\'; esc[1]='r'; n=2; }
        else if (c == '\t') { esc[0]='\\'; esc[1]='t'; n=2; }
        else if (c < 0x20) { n = snprintf(esc, sizeof esc, "\\u%04x", c); }
        if (n) sb_appendn(sb, esc, n);
        else sb_appendn(sb, p, 1);
    }
    sb_append(sb, "\"");
}

void json_emit_rpc_header(SB* sb, int content_len) {
    sb_appendf(sb, "Content-Length: %d\r\n\r\n", content_len);
}
