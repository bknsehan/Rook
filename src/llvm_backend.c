#include "llvm_backend.h"
#include "ast.h"
#include "sema.h"
#include "backend.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef ROKADE_HAS_LLVM

#include <llvm-c/Core.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include <llvm-c/ExecutionEngine.h>
#include <llvm-c/Support.h>
#include <llvm-c/IRReader.h>
#include <llvm-c/Linker.h>

typedef struct DeferFrame {
    Stmt* stmts[32];
    int count;
    int loop_depth;
} DeferFrame;

typedef struct LLVMGen {
    LLVMContextRef ctx;
    LLVMModuleRef module;
    LLVMBuilderRef builder;
    Sema* sema;
    Program* prog;

    LLVMValueRef cur_fn;
    LLVMTypeRef cur_ret_type;
    AstType* cur_ast_ret;

    char* local_names[256];
    LLVMValueRef local_allocas[256];
    LLVMTypeRef local_types[256];
    AstType* local_ast_types[256];
    int nlocals;

    DeferFrame defer_stack[64];
    int defer_depth;
    int loop_depth;

    LLVMBasicBlockRef loop_cond_bbs[32];
    LLVMBasicBlockRef loop_exit_bbs[32];
} LLVMGen;

static int variant_index(EnumDef* ed, const char* name) {
    if (!ed || !name) return -1;
    for (int i = 0; i < ed->nvariants; i++)
        if (strcmp(ed->variants[i].name, name) == 0) return i;
    return -1;
}

static int enum_has_payload(EnumDef* ed) {
    if (!ed) return 0;
    for (int i = 0; i < ed->nvariants; i++)
        if (ed->variants[i].nfields > 0) return 1;
    return 0;
}

static LLVMTypeRef gen_llvm_type(LLVMGen* g, AstType* t);
static LLVMValueRef gen_expr(LLVMGen* g, Expr* e, LLVMTypeRef* out_type);
static LLVMValueRef gen_lvalue(LLVMGen* g, Expr* e, LLVMTypeRef* out_type);
static LLVMValueRef gen_match(LLVMGen* g, Expr* scrut_expr, MatchArm* marms, int nmarms, AstType* result_ast_type, LLVMTypeRef* out_type);
static void gen_stmt(LLVMGen* g, Stmt* s);

static int is_block_terminated(LLVMGen* g) {
    LLVMBasicBlockRef cur = LLVMGetInsertBlock(g->builder);
    return cur ? (LLVMGetBasicBlockTerminator(cur) != NULL) : 1;
}

static void gen_add_local(LLVMGen* g, const char* name, LLVMValueRef alloca_ref, LLVMTypeRef type, AstType* ast_t) {
    if (g->nlocals < 256) {
        g->local_names[g->nlocals] = strdup(name);
        g->local_allocas[g->nlocals] = alloca_ref;
        g->local_types[g->nlocals] = type;
        g->local_ast_types[g->nlocals] = ast_t;
        g->nlocals++;
    }
}

static int gen_find_local(LLVMGen* g, const char* name) {
    for (int i = g->nlocals - 1; i >= 0; i--) {
        if (strcmp(g->local_names[i], name) == 0) return i;
    }
    return -1;
}

static void gen_push_defer_frame(LLVMGen* g) {
    if (g->defer_depth < 64) {
        g->defer_stack[g->defer_depth].count = 0;
        g->defer_stack[g->defer_depth].loop_depth = g->loop_depth;
        g->defer_depth++;
    }
}

static void gen_add_defer(LLVMGen* g, Stmt* s) {
    if (g->defer_depth > 0) {
        DeferFrame* f = &g->defer_stack[g->defer_depth - 1];
        if (f->count < 32) f->stmts[f->count++] = s;
    }
}

static void gen_flush_defer_frame(LLVMGen* g, DeferFrame* f) {
    for (int i = f->count - 1; i >= 0; i--) {
        gen_stmt(g, f->stmts[i]);
    }
}

static void gen_pop_defer_frame(LLVMGen* g) {
    if (g->defer_depth > 0) {
        g->defer_depth--;
        gen_flush_defer_frame(g, &g->defer_stack[g->defer_depth]);
    }
}

static void gen_flush_all_defers(LLVMGen* g) {
    for (int d = g->defer_depth - 1; d >= 0; d--) {
        gen_flush_defer_frame(g, &g->defer_stack[d]);
    }
}

static void gen_flush_loop_defers(LLVMGen* g) {
    for (int d = g->defer_depth - 1; d >= 0; d--) {
        if (g->defer_stack[d].loop_depth >= g->loop_depth) {
            gen_flush_defer_frame(g, &g->defer_stack[d]);
        }
    }
}

static char* unescape_string_literal(const char* s, size_t* out_len) {
    size_t in_len = strlen(s);
    if (in_len < 2 || s[0] != '"') return strdup(s);
    char* buf = malloc(in_len);
    size_t o = 0;
    for (size_t i = 1; i < in_len - 1; i++) {
        if (s[i] == '\\' && i + 1 < in_len - 1) {
            i++;
            switch (s[i]) {
                case 'n': buf[o++] = '\n'; break;
                case 't': buf[o++] = '\t'; break;
                case 'r': buf[o++] = '\r'; break;
                case '0': buf[o++] = '\0'; break;
                case 'a': buf[o++] = '\a'; break;
                case 'b': buf[o++] = '\b'; break;
                case 'f': buf[o++] = '\f'; break;
                case 'v': buf[o++] = '\v'; break;
                case '\\': buf[o++] = '\\'; break;
                case '"': buf[o++] = '"'; break;
                case '\'': buf[o++] = '\''; break;
                case 'x': {
                    if (i + 2 < in_len - 1 && isxdigit((unsigned char)s[i+1]) && isxdigit((unsigned char)s[i+2])) {
                        char hex[3] = { s[i+1], s[i+2], 0 };
                        buf[o++] = (char)strtol(hex, NULL, 16);
                        i += 2;
                    } else if (i + 1 < in_len - 1 && isxdigit((unsigned char)s[i+1])) {
                        char hex[2] = { s[i+1], 0 };
                        buf[o++] = (char)strtol(hex, NULL, 16);
                        i += 1;
                    } else {
                        buf[o++] = s[i];
                    }
                    break;
                }
                default: buf[o++] = s[i]; break;
            }
        } else {
            buf[o++] = s[i];
        }
    }
    buf[o] = '\0';
    if (out_len) *out_len = o;
    return buf;
}

static LLVMTypeRef gen_llvm_type(LLVMGen* g, AstType* t) {
    if (!t || !t->name || strcmp(t->name, "void") == 0) {
        if (t && t->ptrs > 0) return LLVMPointerTypeInContext(g->ctx, 0);
        return LLVMVoidTypeInContext(g->ctx);
    }
    if (t->ptrs > 0) {
        return LLVMPointerTypeInContext(g->ctx, 0);
    }
    const char* n = t->name;
    if (strcmp(n, "int") == 0 || strcmp(n, "int32_t") == 0 || strcmp(n, "uint32_t") == 0) {
        return LLVMInt32TypeInContext(g->ctx);
    }
    if (strcmp(n, "long") == 0 || strcmp(n, "int64_t") == 0 || strcmp(n, "uint64_t") == 0 ||
        strcmp(n, "size_t") == 0 || strcmp(n, "ssize_t") == 0 || strcmp(n, "uintptr_t") == 0 || strcmp(n, "intptr_t") == 0) {
        return LLVMInt64TypeInContext(g->ctx);
    }
    if (strcmp(n, "short") == 0 || strcmp(n, "int16_t") == 0 || strcmp(n, "uint16_t") == 0) {
        return LLVMInt16TypeInContext(g->ctx);
    }
    if (strcmp(n, "char") == 0 || strcmp(n, "int8_t") == 0 || strcmp(n, "uint8_t") == 0 || strcmp(n, "bool") == 0) {
        return LLVMInt8TypeInContext(g->ctx);
    }
    if (strcmp(n, "float") == 0) {
        return LLVMFloatTypeInContext(g->ctx);
    }
    if (strcmp(n, "double") == 0) {
        return LLVMDoubleTypeInContext(g->ctx);
    }

    LLVMTypeRef named = LLVMGetTypeByName2(g->ctx, n);
    if (named) return named;

    StructDef* st = sema_lookup_struct(g->sema, n);
    if (st) {
        LLVMTypeRef st_type = LLVMStructCreateNamed(g->ctx, n);
        int has_parent = (st->parent && st->parent[0]);
        int total_fields = st->nfields + (has_parent ? 1 : 0);
        LLVMTypeRef* elem_types = calloc(total_fields > 0 ? total_fields : 1, sizeof(LLVMTypeRef));
        int fidx = 0;
        if (has_parent) {
            elem_types[fidx++] = gen_llvm_type(g, sema_mk_type("", st->parent, 0));
        }
        for (int i = 0; i < st->nfields; i++) {
            elem_types[fidx++] = gen_llvm_type(g, st->fields[i].type);
        }
        LLVMStructSetBody(st_type, elem_types, total_fields, 0);
        free(elem_types);
        return st_type;
    }

    EnumDef* ed = sema_lookup_enum(g->sema, n);
    if (ed) {
        if (ed->is_c_enum || !enum_has_payload(ed)) {
            return LLVMInt32TypeInContext(g->ctx);
        }
        LLVMTypeRef st_type = LLVMStructCreateNamed(g->ctx, n);
        LLVMTypeRef elems[2] = {
            LLVMInt32TypeInContext(g->ctx),
            LLVMArrayType(LLVMInt8TypeInContext(g->ctx), 32)
        };
        LLVMStructSetBody(st_type, elems, 2, 0);
        return st_type;
    }

    const char* en = sema_lookup_variant(g->sema, n);
    if (en) {
        AstType parent_at = { .qual = "", .name = (char*)en, .ptrs = 0 };
        return gen_llvm_type(g, &parent_at);
    }

    return LLVMInt32TypeInContext(g->ctx);
}

static LLVMValueRef cast_to_type(LLVMGen* g, LLVMValueRef val, LLVMTypeRef from, LLVMTypeRef to) {
    if (!val || !from || !to) return val;
    if (from == to) return val;
    LLVMTypeKind fk = LLVMGetTypeKind(from);
    LLVMTypeKind tk = LLVMGetTypeKind(to);

    if (fk == LLVMIntegerTypeKind && tk == LLVMIntegerTypeKind) {
        unsigned fw = LLVMGetIntTypeWidth(from);
        if (fw == 1) {
            return LLVMBuildZExt(g->builder, val, to, "zext");
        }
        return LLVMBuildIntCast2(g->builder, val, to, 1, "intcast");
    }
    if (fk == LLVMIntegerTypeKind && (tk == LLVMFloatTypeKind || tk == LLVMDoubleTypeKind)) {
        unsigned fw = LLVMGetIntTypeWidth(from);
        if (fw == 1) {
            val = LLVMBuildZExt(g->builder, val, LLVMInt32TypeInContext(g->ctx), "zext");
        }
        return LLVMBuildSIToFP(g->builder, val, to, "sitofp");
    }
    if ((fk == LLVMFloatTypeKind || fk == LLVMDoubleTypeKind) && tk == LLVMIntegerTypeKind) {
        return LLVMBuildFPToSI(g->builder, val, to, "fptosi");
    }
    if (fk == LLVMFloatTypeKind && tk == LLVMDoubleTypeKind) {
        return LLVMBuildFPExt(g->builder, val, to, "fpext");
    }
    if (fk == LLVMDoubleTypeKind && tk == LLVMFloatTypeKind) {
        return LLVMBuildFPTrunc(g->builder, val, to, "fptrunc");
    }
    if (fk == LLVMPointerTypeKind && tk == LLVMIntegerTypeKind) {
        return LLVMBuildPtrToInt(g->builder, val, to, "ptrtoint");
    }
    if (fk == LLVMIntegerTypeKind && tk == LLVMPointerTypeKind) {
        return LLVMBuildIntToPtr(g->builder, val, to, "inttoptr");
    }
    if (fk == LLVMPointerTypeKind && tk == LLVMPointerTypeKind) {
        return val;
    }
    return val;
}

static char* llvm_find_method_owner(LLVMGen* g, const char* struct_name, const char* method, int* steps) {
    *steps = 0;
    const char* cur = struct_name;
    while (cur) {
        size_t tlen = strlen(cur) + strlen(method) + 2;
        char* test_name = malloc(tlen);
        if (test_name) {
            snprintf(test_name, tlen, "%s_%s", cur, method);
            if (LLVMGetNamedFunction(g->module, test_name)) {
                free(test_name);
                return strdup(cur);
            }
            free(test_name);
        }
        StructDef* st = sema_lookup_struct(g->sema, cur);
        if (!st || !st->parent || !st->parent[0]) break;
        cur = st->parent;
        (*steps)++;
    }
    return NULL;
}

static AstType* llvm_resolve_expr_type(LLVMGen* g, Expr* e) {
    if (!e) return NULL;
    if (e->type) return e->type;
    if (e->kind == E_IDENT) {
        int li = gen_find_local(g, e->str);
        if (li >= 0 && g->local_ast_types[li]) return g->local_ast_types[li];
        Sym* s = sema_lookup(g->sema, e->str);
        if (s) {
            if (s->type) return s->type;
            if (s->fn && s->fn->ret) return s->fn->ret;
        }
        EnumDef* ed = sema_lookup_enum(g->sema, e->str);
        if (ed) return sema_mk_type("", ed->name, 0);
        const char* en = sema_lookup_variant(g->sema, e->str);
        if (en) return sema_mk_type("", en, 0);
    } else if (e->kind == E_UNARY && e->str) {
        if (strcmp(e->str, "*") == 0) {
            AstType* sub = llvm_resolve_expr_type(g, e->a);
            if (sub && sub->ptrs > 0) {
                return sema_mk_type(sub->qual ? sub->qual : "", sub->name, sub->ptrs - 1);
            }
        } else if (strcmp(e->str, "&") == 0) {
            AstType* sub = llvm_resolve_expr_type(g, e->a);
            if (sub) {
                return sema_mk_type(sub->qual ? sub->qual : "", sub->name, sub->ptrs + 1);
            }
        }
    } else if ((e->kind == E_MEMBER || e->kind == E_ARROW) && e->str) {
        AstType* sub = llvm_resolve_expr_type(g, e->a);
        if (sub && sub->name) {
            StructDef* st = sema_lookup_struct(g->sema, sub->name);
            if (st) {
                for (int i = 0; i < st->nfields; i++) {
                    if (strcmp(st->fields[i].name, e->str) == 0) {
                        return st->fields[i].type;
                    }
                }
            }
        }
    }
    return sema_resolve_type(g->sema, e);
}

static LLVMValueRef gen_lvalue(LLVMGen* g, Expr* e, LLVMTypeRef* out_type) {
    if (!e) return NULL;
    if (e->kind == E_IDENT) {
        int idx = gen_find_local(g, e->str);
        if (idx >= 0) {
            if (out_type) *out_type = g->local_types[idx];
            return g->local_allocas[idx];
        }
        LLVMValueRef gv = LLVMGetNamedGlobal(g->module, e->str);
        if (gv) {
            if (out_type) *out_type = LLVMGlobalGetValueType(gv);
            return gv;
        }
    } else if (e->kind == E_MEMBER || e->kind == E_ARROW) {
        AstType* at = NULL;
        int local_idx = -1;
        if (e->a->kind == E_IDENT) {
            local_idx = gen_find_local(g, e->a->str);
            if (local_idx >= 0) at = g->local_ast_types[local_idx];
        }
        int allocated_at = 0;
        if (!at) {
            at = sema_resolve_type(g->sema, e->a);
            allocated_at = 1;
        }
        const char* st_name = at ? at->name : NULL;
        StructDef* st = st_name ? sema_lookup_struct(g->sema, st_name) : NULL;
        if (st) {
            LLVMValueRef cur_ptr = NULL;
            if (at && at->ptrs > 0) {
                cur_ptr = gen_expr(g, e->a, NULL);
            } else {
                cur_ptr = gen_lvalue(g, e->a, NULL);
                if (!cur_ptr) cur_ptr = gen_expr(g, e->a, NULL);
            }

            StructDef* cur_st = st;
            while (cur_st && cur_ptr) {
                int has_parent = (cur_st->parent && cur_st->parent[0]);
                int offset = has_parent ? 1 : 0;
                int found = -1;
                for (int i = 0; i < cur_st->nfields; i++) {
                    if (strcmp(cur_st->fields[i].name, e->str) == 0) {
                        found = i;
                        break;
                    }
                }
                AstType cur_unptr = { .qual = "", .name = cur_st->name, .ptrs = 0 };
                LLVMTypeRef cur_st_ll = gen_llvm_type(g, &cur_unptr);
                if (found >= 0) {
                    LLVMValueRef fptr = LLVMBuildStructGEP2(g->builder, cur_st_ll, cur_ptr, found + offset, e->str);
                    if (out_type) *out_type = gen_llvm_type(g, cur_st->fields[found].type);
                    if (allocated_at && at) free(at);
                    return fptr;
                }
                if (has_parent) {
                    cur_ptr = LLVMBuildStructGEP2(g->builder, cur_st_ll, cur_ptr, 0, "_base");
                    cur_st = sema_lookup_struct(g->sema, cur_st->parent);
                } else {
                    break;
                }
            }
        }
        if (allocated_at && at) free(at);
    } else if (e->kind == E_INDEX) {
        LLVMTypeRef base_type = NULL;
        LLVMValueRef base_ptr = gen_expr(g, e->a, &base_type);
        LLVMValueRef idx_val = gen_expr(g, e->b, NULL);
        if (base_ptr && idx_val) {
            AstType* at = NULL;
            int allocated_at = 0;
            if (e->a && e->a->kind == E_IDENT) {
                int li = gen_find_local(g, e->a->str);
                if (li >= 0) at = g->local_ast_types[li];
            }
            if (!at) {
                at = sema_resolve_type(g->sema, e->a);
                allocated_at = 1;
            }
            AstType elem_at = {0};
            if (at) {
                elem_at.name = at->name;
                elem_at.ptrs = at->ptrs > 0 ? at->ptrs - 1 : 0;
            }
            LLVMTypeRef elem_type = at ? gen_llvm_type(g, &elem_at) : LLVMInt8TypeInContext(g->ctx);
            LLVMValueRef indices[1] = { idx_val };
            LLVMValueRef gep = LLVMBuildGEP2(g->builder, elem_type, base_ptr, indices, 1, "idxgep");
            if (out_type) *out_type = elem_type;
            if (allocated_at && at) free(at);
            return gep;
        }
    } else if (e->kind == E_UNARY && e->str && strcmp(e->str, "*") == 0) {
        LLVMTypeRef ptr_type = NULL;
        LLVMValueRef ptr_val = gen_expr(g, e->a, &ptr_type);
        AstType* at = llvm_resolve_expr_type(g, e);
        if (out_type) *out_type = at ? gen_llvm_type(g, at) : LLVMInt32TypeInContext(g->ctx);
        return ptr_val;
    }
    return NULL;
}

static LLVMValueRef gen_expr(LLVMGen* g, Expr* e, LLVMTypeRef* out_type) {
    if (!e) return NULL;

    switch (e->kind) {
    case E_LITERAL: {
        const char* s = e->str;
        if (!s) return NULL;
        if (s[0] == '\"') {
            size_t slen = 0;
            char* u = unescape_string_literal(s, &slen);
            /* Use LLVMConstStringInContext so embedded \0 bytes survive; add null terminator. */
            LLVMValueRef str_const = LLVMConstStringInContext(g->ctx, u, (unsigned)slen, 0);
            free(u);
            LLVMTypeRef str_arr_t = LLVMTypeOf(str_const);
            LLVMValueRef global = LLVMAddGlobal(g->module, str_arr_t, "str");
            LLVMSetInitializer(global, str_const);
            LLVMSetGlobalConstant(global, 1);
            LLVMSetLinkage(global, LLVMPrivateLinkage);
            LLVMSetUnnamedAddress(global, LLVMGlobalUnnamedAddr);
            if (out_type) *out_type = LLVMPointerTypeInContext(g->ctx, 0);
            return global;
        }
        if (s[0] == '\'') {
            /* Decode escape sequence in char literal */
            int ch;
            if (s[1] == '\\') {
                switch (s[2]) {
                    case 'n':  ch = '\n'; break;
                    case 't':  ch = '\t'; break;
                    case 'r':  ch = '\r'; break;
                    case '0':  ch = '\0'; break;
                    case 'a':  ch = '\a'; break;
                    case 'b':  ch = '\b'; break;
                    case 'f':  ch = '\f'; break;
                    case 'v':  ch = '\v'; break;
                    case '\\': ch = '\\'; break;
                    case '\'': ch = '\''; break;
                    case 'x': {
                        char hex[3] = { s[3], s[4], 0 };
                        ch = (int)strtol(hex, NULL, 16);
                        break;
                    }
                    default: ch = (unsigned char)s[2]; break;
                }
            } else {
                ch = (unsigned char)s[1];
            }
            LLVMValueRef c_val = LLVMConstInt(LLVMInt8TypeInContext(g->ctx), (unsigned long long)(unsigned char)ch, 0);
            if (out_type) *out_type = LLVMInt8TypeInContext(g->ctx);
            return c_val;
        }
        if (strchr(s, '.') || strchr(s, 'e') || strchr(s, 'E')) {
            double d = strtod(s, NULL);
            size_t sl = strlen(s);
            if (s[sl - 1] == 'f' || s[sl - 1] == 'F') {
                LLVMValueRef f_val = LLVMConstReal(LLVMFloatTypeInContext(g->ctx), (float)d);
                if (out_type) *out_type = LLVMFloatTypeInContext(g->ctx);
                return f_val;
            } else {
                LLVMValueRef d_val = LLVMConstReal(LLVMDoubleTypeInContext(g->ctx), d);
                if (out_type) *out_type = LLVMDoubleTypeInContext(g->ctx);
                return d_val;
            }
        }
        long long iv = strtoll(s, NULL, 0);
        LLVMValueRef i_val = LLVMConstInt(LLVMInt32TypeInContext(g->ctx), (unsigned long long)iv, 1);
        if (out_type) *out_type = LLVMInt32TypeInContext(g->ctx);
        return i_val;
    }

    case E_IDENT: {
        const char* name = e->str;
        if (strcmp(name, "true") == 0) {
            if (out_type) *out_type = LLVMInt8TypeInContext(g->ctx);
            return LLVMConstInt(LLVMInt8TypeInContext(g->ctx), 1, 0);
        }
        if (strcmp(name, "false") == 0) {
            if (out_type) *out_type = LLVMInt8TypeInContext(g->ctx);
            return LLVMConstInt(LLVMInt8TypeInContext(g->ctx), 0, 0);
        }
        if (strcmp(name, "NULL") == 0 || strcmp(name, "null") == 0) {
            if (out_type) *out_type = LLVMPointerTypeInContext(g->ctx, 0);
            return LLVMConstNull(LLVMPointerTypeInContext(g->ctx, 0));
        }

        int idx = gen_find_local(g, name);
        if (idx >= 0) {
            LLVMTypeRef lt = g->local_types[idx];
            LLVMValueRef alloca_ref = g->local_allocas[idx];
            if (LLVMGetTypeKind(lt) == LLVMArrayTypeKind) {
                if (out_type) *out_type = LLVMPointerTypeInContext(g->ctx, 0);
                LLVMValueRef indices[2] = {
                    LLVMConstInt(LLVMInt64TypeInContext(g->ctx), 0, 0),
                    LLVMConstInt(LLVMInt64TypeInContext(g->ctx), 0, 0)
                };
                return LLVMBuildGEP2(g->builder, lt, alloca_ref, indices, 2, name);
            }
            if (out_type) *out_type = lt;
            return LLVMBuildLoad2(g->builder, lt, alloca_ref, name);
        }

        LLVMValueRef gv = LLVMGetNamedGlobal(g->module, name);
        if (gv) {
            LLVMTypeRef vt = LLVMGlobalGetValueType(gv);
            if (out_type) *out_type = vt;
            return LLVMBuildLoad2(g->builder, vt, gv, name);
        }

        Sym* sym = sema_lookup(g->sema, name);
        if (sym && sym->kind == SYM_VAR) {
            LLVMValueRef existing_gv = LLVMGetNamedGlobal(g->module, name);
            if (existing_gv) {
                LLVMTypeRef gvt = LLVMGlobalGetValueType(existing_gv);
                if (out_type) *out_type = gvt;
                return LLVMBuildLoad2(g->builder, gvt, existing_gv, name);
            }
            LLVMTypeRef gvt = sym->type ? gen_llvm_type(g, sym->type) : LLVMPointerTypeInContext(g->ctx, 0);
            LLVMValueRef new_gv = LLVMAddGlobal(g->module, gvt, name);
            LLVMSetLinkage(new_gv, LLVMExternalLinkage);
            if (out_type) *out_type = gvt;
            return LLVMBuildLoad2(g->builder, gvt, new_gv, name);
        }
        if (sym && sym->kind == SYM_ENUMVARIANT) {
            EnumDef* ed = sym->ed ? sym->ed : sema_lookup_enum(g->sema, sema_lookup_variant(g->sema, name));
            if (ed && enum_has_payload(ed)) {
                /* Unit variant constructor of a sum enum: e.g. Point */
                AstType enum_at = { .qual = "", .name = ed->name, .ptrs = 0 };
                LLVMTypeRef enum_t = gen_llvm_type(g, &enum_at);
                LLVMValueRef sum_alloca = LLVMBuildAlloca(g->builder, enum_t, "unit_sum_init");
                LLVMBuildStore(g->builder, LLVMConstNull(enum_t), sum_alloca);
                LLVMValueRef tag_ptr = LLVMBuildStructGEP2(g->builder, enum_t, sum_alloca, 0, "tag_ptr");
                LLVMBuildStore(g->builder, LLVMConstInt(LLVMInt32TypeInContext(g->ctx), (unsigned long long)sym->variant_idx, 0), tag_ptr);
                if (out_type) *out_type = enum_t;
                return LLVMBuildLoad2(g->builder, enum_t, sum_alloca, "load_unit_sum");
            }
            if (out_type) *out_type = LLVMInt32TypeInContext(g->ctx);
            return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), (unsigned long long)sym->variant_idx, 0);
        }

        LLVMValueRef fn = LLVMGetNamedFunction(g->module, name);
        if (fn) {
            if (out_type) *out_type = LLVMPointerTypeInContext(g->ctx, 0);
            return fn;
        }

        /* Default fallback integer 0 */
        if (out_type) *out_type = LLVMInt32TypeInContext(g->ctx);
        return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
    }

    case E_BINARY: {
        LLVMTypeRef ta = NULL;
        LLVMTypeRef tb = NULL;
        LLVMValueRef va = gen_expr(g, e->a, &ta);
        LLVMValueRef vb = gen_expr(g, e->b, &tb);
        if (!va || !vb) return NULL;

        LLVMTypeKind ka = LLVMGetTypeKind(ta);
        LLVMTypeKind kb = LLVMGetTypeKind(tb);

        int is_float = (ka == LLVMFloatTypeKind || ka == LLVMDoubleTypeKind ||
                        kb == LLVMFloatTypeKind || kb == LLVMDoubleTypeKind);

        if (is_float) {
            LLVMTypeRef ftype = (ka == LLVMDoubleTypeKind || kb == LLVMDoubleTypeKind)
                                ? LLVMDoubleTypeInContext(g->ctx) : LLVMFloatTypeInContext(g->ctx);
            va = cast_to_type(g, va, ta, ftype);
            vb = cast_to_type(g, vb, tb, ftype);

            const char* op = e->str;
            if (strcmp(op, "+") == 0) { if (out_type) *out_type = ftype; return LLVMBuildFAdd(g->builder, va, vb, "fadd"); }
            if (strcmp(op, "-") == 0) { if (out_type) *out_type = ftype; return LLVMBuildFSub(g->builder, va, vb, "fsub"); }
            if (strcmp(op, "*") == 0) { if (out_type) *out_type = ftype; return LLVMBuildFMul(g->builder, va, vb, "fmul"); }
            if (strcmp(op, "/") == 0) { if (out_type) *out_type = ftype; return LLVMBuildFDiv(g->builder, va, vb, "fdiv"); }
            if (strcmp(op, "%") == 0) { if (out_type) *out_type = ftype; return LLVMBuildFRem(g->builder, va, vb, "frem"); }
            if (strcmp(op, "==") == 0) { if (out_type) *out_type = LLVMInt1TypeInContext(g->ctx); return LLVMBuildFCmp(g->builder, LLVMRealOEQ, va, vb, "fcmp"); }
            if (strcmp(op, "!=") == 0) { if (out_type) *out_type = LLVMInt1TypeInContext(g->ctx); return LLVMBuildFCmp(g->builder, LLVMRealONE, va, vb, "fcmp"); }
            if (strcmp(op, "<") == 0)  { if (out_type) *out_type = LLVMInt1TypeInContext(g->ctx); return LLVMBuildFCmp(g->builder, LLVMRealOLT, va, vb, "fcmp"); }
            if (strcmp(op, "<=") == 0) { if (out_type) *out_type = LLVMInt1TypeInContext(g->ctx); return LLVMBuildFCmp(g->builder, LLVMRealOLE, va, vb, "fcmp"); }
            if (strcmp(op, ">") == 0)  { if (out_type) *out_type = LLVMInt1TypeInContext(g->ctx); return LLVMBuildFCmp(g->builder, LLVMRealOGT, va, vb, "fcmp"); }
            if (strcmp(op, ">=") == 0) { if (out_type) *out_type = LLVMInt1TypeInContext(g->ctx); return LLVMBuildFCmp(g->builder, LLVMRealOGE, va, vb, "fcmp"); }
        } else {
            LLVMTypeRef itype = (ta == LLVMInt64TypeInContext(g->ctx) || tb == LLVMInt64TypeInContext(g->ctx))
                                ? LLVMInt64TypeInContext(g->ctx) : LLVMInt32TypeInContext(g->ctx);
            va = cast_to_type(g, va, ta, itype);
            vb = cast_to_type(g, vb, tb, itype);

            const char* op = e->str;
            if (strcmp(op, "+") == 0) { if (out_type) *out_type = itype; return LLVMBuildAdd(g->builder, va, vb, "add"); }
            if (strcmp(op, "-") == 0) { if (out_type) *out_type = itype; return LLVMBuildSub(g->builder, va, vb, "sub"); }
            if (strcmp(op, "*") == 0) { if (out_type) *out_type = itype; return LLVMBuildMul(g->builder, va, vb, "mul"); }
            if (strcmp(op, "/") == 0) { if (out_type) *out_type = itype; return LLVMBuildSDiv(g->builder, va, vb, "sdiv"); }
            if (strcmp(op, "%") == 0) { if (out_type) *out_type = itype; return LLVMBuildSRem(g->builder, va, vb, "srem"); }
            if (strcmp(op, "&") == 0) { if (out_type) *out_type = itype; return LLVMBuildAnd(g->builder, va, vb, "and"); }
            if (strcmp(op, "|") == 0) { if (out_type) *out_type = itype; return LLVMBuildOr(g->builder, va, vb, "or"); }
            if (strcmp(op, "^") == 0) { if (out_type) *out_type = itype; return LLVMBuildXor(g->builder, va, vb, "xor"); }
            if (strcmp(op, "<<") == 0) { if (out_type) *out_type = itype; return LLVMBuildShl(g->builder, va, vb, "shl"); }
            if (strcmp(op, ">>") == 0) { if (out_type) *out_type = itype; return LLVMBuildAShr(g->builder, va, vb, "ashr"); }

            if (strcmp(op, "==") == 0) { if (out_type) *out_type = LLVMInt1TypeInContext(g->ctx); return LLVMBuildICmp(g->builder, LLVMIntEQ, va, vb, "icmp"); }
            if (strcmp(op, "!=") == 0) { if (out_type) *out_type = LLVMInt1TypeInContext(g->ctx); return LLVMBuildICmp(g->builder, LLVMIntNE, va, vb, "icmp"); }
            if (strcmp(op, "<") == 0)  { if (out_type) *out_type = LLVMInt1TypeInContext(g->ctx); return LLVMBuildICmp(g->builder, LLVMIntSLT, va, vb, "icmp"); }
            if (strcmp(op, "<=") == 0) { if (out_type) *out_type = LLVMInt1TypeInContext(g->ctx); return LLVMBuildICmp(g->builder, LLVMIntSLE, va, vb, "icmp"); }
            if (strcmp(op, ">") == 0)  { if (out_type) *out_type = LLVMInt1TypeInContext(g->ctx); return LLVMBuildICmp(g->builder, LLVMIntSGT, va, vb, "icmp"); }
            if (strcmp(op, ">=") == 0) { if (out_type) *out_type = LLVMInt1TypeInContext(g->ctx); return LLVMBuildICmp(g->builder, LLVMIntSGE, va, vb, "icmp"); }

            if (strcmp(op, "&&") == 0) {
                LLVMValueRef a_bool = LLVMBuildICmp(g->builder, LLVMIntNE, va, LLVMConstInt(itype, 0, 0), "abool");
                LLVMValueRef b_bool = LLVMBuildICmp(g->builder, LLVMIntNE, vb, LLVMConstInt(itype, 0, 0), "bbool");
                LLVMValueRef res = LLVMBuildAnd(g->builder, a_bool, b_bool, "land");
                if (out_type) *out_type = LLVMInt1TypeInContext(g->ctx);
                return res;
            }
            if (strcmp(op, "||") == 0) {
                LLVMValueRef a_bool = LLVMBuildICmp(g->builder, LLVMIntNE, va, LLVMConstInt(itype, 0, 0), "abool");
                LLVMValueRef b_bool = LLVMBuildICmp(g->builder, LLVMIntNE, vb, LLVMConstInt(itype, 0, 0), "bbool");
                LLVMValueRef res = LLVMBuildOr(g->builder, a_bool, b_bool, "lor");
                if (out_type) *out_type = LLVMInt1TypeInContext(g->ctx);
                return res;
            }
        }
        return va;
    }

    case E_UNARY: {
        const char* op = e->str;
        if (strcmp(op, "&") == 0) {
            LLVMTypeRef elem_t = NULL;
            LLVMValueRef lval = gen_lvalue(g, e->a, &elem_t);
            if (out_type) *out_type = LLVMPointerTypeInContext(g->ctx, 0);
            return lval;
        }
        if (strcmp(op, "*") == 0) {
            LLVMTypeRef pt = NULL;
            LLVMValueRef ptr = gen_expr(g, e->a, &pt);
            AstType* at = llvm_resolve_expr_type(g, e->a);
            AstType deref_at = {0};
            if (at) {
                deref_at.qual = at->qual;
                deref_at.name = at->name;
                deref_at.ptrs = at->ptrs > 0 ? at->ptrs - 1 : 0;
            }
            LLVMTypeRef elem_type = at ? gen_llvm_type(g, &deref_at) : LLVMInt32TypeInContext(g->ctx);
            if (out_type) *out_type = elem_type;
            return LLVMBuildLoad2(g->builder, elem_type, ptr, "deref");
        }
        LLVMTypeRef vt = NULL;
        LLVMValueRef val = gen_expr(g, e->a, &vt);
        if (!val) return NULL;
        LLVMTypeKind k = LLVMGetTypeKind(vt);
        if (strcmp(op, "-") == 0) {
            if (k == LLVMFloatTypeKind || k == LLVMDoubleTypeKind) {
                if (out_type) *out_type = vt;
                return LLVMBuildFNeg(g->builder, val, "fneg");
            } else {
                if (out_type) *out_type = vt;
                return LLVMBuildNeg(g->builder, val, "neg");
            }
        }
        if (strcmp(op, "!") == 0) {
            LLVMValueRef is_zero = LLVMBuildICmp(g->builder, LLVMIntEQ, val, LLVMConstNull(vt), "not");
            if (out_type) *out_type = LLVMInt8TypeInContext(g->ctx);
            return is_zero;
        }
        if (strcmp(op, "~") == 0) {
            if (out_type) *out_type = vt;
            return LLVMBuildNot(g->builder, val, "not");
        }
        if (strcmp(op, "++") == 0 || strcmp(op, "--") == 0) {
            LLVMTypeRef lt = NULL;
            LLVMValueRef lptr = gen_lvalue(g, e->a, &lt);
            if (lptr) {
                LLVMValueRef one = (k == LLVMFloatTypeKind || k == LLVMDoubleTypeKind)
                                   ? LLVMConstReal(vt, 1.0) : LLVMConstInt(vt, 1, 0);
                LLVMValueRef updated = (strcmp(op, "++") == 0)
                                       ? ((k == LLVMFloatTypeKind || k == LLVMDoubleTypeKind) ? LLVMBuildFAdd(g->builder, val, one, "inc") : LLVMBuildAdd(g->builder, val, one, "inc"))
                                       : ((k == LLVMFloatTypeKind || k == LLVMDoubleTypeKind) ? LLVMBuildFSub(g->builder, val, one, "dec") : LLVMBuildSub(g->builder, val, one, "dec"));
                LLVMBuildStore(g->builder, updated, lptr);
                if (out_type) *out_type = vt;
                return updated;
            }
        }
        return val;
    }

    case E_POST: {
        const char* op = e->str;
        LLVMTypeRef vt = NULL;
        LLVMValueRef val = gen_expr(g, e->a, &vt);
        LLVMValueRef lptr = gen_lvalue(g, e->a, &vt);
        if (lptr && val) {
            LLVMTypeKind k = LLVMGetTypeKind(vt);
            LLVMValueRef one = (k == LLVMFloatTypeKind || k == LLVMDoubleTypeKind)
                               ? LLVMConstReal(vt, 1.0) : LLVMConstInt(vt, 1, 0);
            LLVMValueRef updated = (strcmp(op, "++") == 0)
                                   ? ((k == LLVMFloatTypeKind || k == LLVMDoubleTypeKind) ? LLVMBuildFAdd(g->builder, val, one, "inc") : LLVMBuildAdd(g->builder, val, one, "inc"))
                                   : ((k == LLVMFloatTypeKind || k == LLVMDoubleTypeKind) ? LLVMBuildFSub(g->builder, val, one, "dec") : LLVMBuildSub(g->builder, val, one, "dec"));
            LLVMBuildStore(g->builder, updated, lptr);
        }
        if (out_type) *out_type = vt;
        return val;
    }

    case E_CALL: {
        if (e->a->kind == E_IDENT) {
            const char* en = sema_lookup_variant(g->sema, e->a->str);
            if (en) {
                /* tuple-style sum variant constructor: `Circle(2.0)` or `Point()` */
                EnumDef* ed = sema_lookup_enum(g->sema, en);
                int vi = ed ? variant_index(ed, e->a->str) : -1;
                if (ed && vi >= 0) {
                    EnumVariant* v = &ed->variants[vi];
                    AstType parent_at = { .qual = "", .name = (char*)en, .ptrs = 0 };
                    LLVMTypeRef enum_t = gen_llvm_type(g, &parent_at);
                    LLVMValueRef sum_alloca = LLVMBuildAlloca(g->builder, enum_t, "sum_init");
                    LLVMBuildStore(g->builder, LLVMConstNull(enum_t), sum_alloca);

                    LLVMValueRef tag_ptr = LLVMBuildStructGEP2(g->builder, enum_t, sum_alloca, 0, "tag_ptr");
                    LLVMBuildStore(g->builder, LLVMConstInt(LLVMInt32TypeInContext(g->ctx), (unsigned long long)vi, 0), tag_ptr);

                    if (v->nfields > 0) {
                        LLVMValueRef payload_ptr = LLVMBuildStructGEP2(g->builder, enum_t, sum_alloca, 1, "payload_buf");
                        LLVMTypeRef* vft = calloc(v->nfields, sizeof(LLVMTypeRef));
                        for (int k = 0; k < v->nfields; k++) vft[k] = gen_llvm_type(g, v->fields[k].type);
                        LLVMTypeRef vpayload_t = LLVMStructTypeInContext(g->ctx, vft, v->nfields, 0);

                        for (int k = 0; k < e->nitems && k < v->nfields; k++) {
                            LLVMValueRef fptr = LLVMBuildStructGEP2(g->builder, vpayload_t, payload_ptr, k, v->fields[k].name);
                            LLVMTypeRef rt = NULL;
                            LLVMValueRef fval = gen_expr(g, e->items[k], &rt);
                            fval = cast_to_type(g, fval, rt, vft[k]);
                            LLVMBuildStore(g->builder, fval, fptr);
                        }
                        free(vft);
                    }
                    if (out_type) *out_type = enum_t;
                    return LLVMBuildLoad2(g->builder, enum_t, sum_alloca, "load_sum");
                }
            }
        }

        char fn_name[256] = "";
        int inherit_steps = 0;
        char* method_owner = NULL;
        if (e->a->kind == E_IDENT) {
            snprintf(fn_name, sizeof(fn_name), "%s", e->a->str);
        } else if (e->a->kind == E_MEMBER) {
            AstType* at = NULL;
            if (e->a->a->kind == E_IDENT) {
                int local_idx = gen_find_local(g, e->a->a->str);
                if (local_idx >= 0) at = g->local_ast_types[local_idx];
            }
            int allocated_at = 0;
            if (!at) {
                at = sema_resolve_type(g->sema, e->a->a);
                allocated_at = 1;
            }
            if (at && at->name) {
                method_owner = llvm_find_method_owner(g, at->name, e->a->str, &inherit_steps);
                if (method_owner) {
                    snprintf(fn_name, sizeof(fn_name), "%s_%s", method_owner, e->a->str);
                } else {
                    snprintf(fn_name, sizeof(fn_name), "%s_%s", at->name, e->a->str);
                }
            }
            if (allocated_at && at) free(at);
        }

        LLVMValueRef fn_val = LLVMGetNamedFunction(g->module, fn_name);
        LLVMTypeRef fn_type = NULL;

        if (!fn_val) {
            const char* c_ret = sema_lookup_cfunc(fn_name);
            int np = sema_cfunc_nparams(fn_name);
            int is_var = sema_cfunc_is_variadic(fn_name);

            AstType ret_at = { .qual = "", .name = (char*)(c_ret ? c_ret : "int"), .ptrs = (c_ret && strchr(c_ret, '*')) ? 1 : 0 };
            LLVMTypeRef ret_t = gen_llvm_type(g, &ret_at);

            LLVMTypeRef* param_ts = calloc(np > 0 ? np : 1, sizeof(LLVMTypeRef));
            for (int i = 0; i < np; i++) {
                const char* pt = sema_lookup_cfunc_param(fn_name, i);
                AstType p_at = { .qual = "", .name = (char*)(pt ? pt : "int"), .ptrs = (pt && strchr(pt, '*')) ? 1 : 0 };
                param_ts[i] = gen_llvm_type(g, &p_at);
            }
            fn_type = LLVMFunctionType(ret_t, param_ts, np >= 0 ? np : 0, is_var);
            fn_val = LLVMAddFunction(g->module, fn_name, fn_type);
            free(param_ts);
        } else {
            fn_type = LLVMGlobalGetValueType(fn_val);
        }

        int nargs = e->nitems;
        int is_method_call = (e->a->kind == E_MEMBER);
        int total_args = nargs + (is_method_call ? 1 : 0);
        LLVMValueRef* args = calloc(total_args > 0 ? total_args : 1, sizeof(LLVMValueRef));

        int arg_idx = 0;
        if (is_method_call) {
            AstType* at = NULL;
            if (e->a->a->kind == E_IDENT) {
                int li = gen_find_local(g, e->a->a->str);
                if (li >= 0) at = g->local_ast_types[li];
            }
            int allocated_at = 0;
            if (!at) {
                at = sema_resolve_type(g->sema, e->a->a);
                allocated_at = 1;
            }
            LLVMValueRef self_ptr = NULL;
            if (at && at->ptrs > 0) {
                self_ptr = gen_expr(g, e->a->a, NULL);
            } else {
                self_ptr = gen_lvalue(g, e->a->a, NULL);
                if (!self_ptr) self_ptr = gen_expr(g, e->a->a, NULL);
            }
            const char* cur_sname = at ? at->name : NULL;
            for (int s = 0; s < inherit_steps && cur_sname; s++) {
                LLVMTypeRef cur_type = LLVMGetTypeByName2(g->ctx, cur_sname);
                if (!cur_type) {
                    AstType cur_at = { .qual = "", .name = (char*)cur_sname, .ptrs = 0 };
                    cur_type = gen_llvm_type(g, &cur_at);
                }
                self_ptr = LLVMBuildStructGEP2(g->builder, cur_type, self_ptr, 0, "base");
                StructDef* cur_st = sema_lookup_struct(g->sema, cur_sname);
                cur_sname = cur_st ? cur_st->parent : NULL;
            }
            if (allocated_at && at) free(at);
            if (method_owner) free(method_owner);
            args[arg_idx++] = self_ptr;
        }

        unsigned pcount = LLVMCountParamTypes(fn_type);
        LLVMTypeRef* ptypes = calloc(pcount > 0 ? pcount : 1, sizeof(LLVMTypeRef));
        if (pcount > 0) LLVMGetParamTypes(fn_type, ptypes);

        for (int i = 0; i < nargs; i++) {
            LLVMTypeRef at = NULL;
            LLVMValueRef arg = gen_expr(g, e->items[i], &at);
            if (arg && at) {
                if (arg_idx < (int)pcount) {
                    arg = cast_to_type(g, arg, at, ptypes[arg_idx]);
                } else if (LLVMIsFunctionVarArg(fn_type)) {
                    if (LLVMGetTypeKind(at) == LLVMFloatTypeKind) {
                        arg = LLVMBuildFPExt(g->builder, arg, LLVMDoubleTypeInContext(g->ctx), "fpext");
                    } else if (LLVMGetTypeKind(at) == LLVMIntegerTypeKind && LLVMGetIntTypeWidth(at) < 32) {
                        arg = LLVMBuildZExt(g->builder, arg, LLVMInt32TypeInContext(g->ctx), "intext");
                    }
                }
            }
            args[arg_idx++] = arg;
        }
        free(ptypes);

        LLVMTypeRef ret_type = LLVMGetReturnType(fn_type);
        int is_void = (LLVMGetTypeKind(ret_type) == LLVMVoidTypeKind);
        LLVMValueRef call_res = LLVMBuildCall2(g->builder, fn_type, fn_val, args, total_args, is_void ? "" : "call");
        free(args);

        if (out_type) *out_type = ret_type;
        return call_res;
    }

    case E_SIZEOF_T: {
        LLVMTypeRef lt = NULL;
        if (e->type && e->type->name) {
            int l_idx = gen_find_local(g, e->type->name);
            if (l_idx >= 0) {
                lt = g->local_types[l_idx];
            }
        }
        if (!lt) {
            lt = gen_llvm_type(g, e->type);
        }
        if (out_type) *out_type = LLVMInt64TypeInContext(g->ctx);
        return LLVMSizeOf(lt);
    }

    case E_SIZEOF_E: {
        LLVMTypeRef lt = NULL;
        if (e->a && e->a->kind == E_IDENT) {
            int l_idx = gen_find_local(g, e->a->str);
            if (l_idx >= 0) {
                lt = g->local_types[l_idx];
            }
        }
        if (!lt) {
            gen_expr(g, e->a, &lt);
        }
        if (!lt) lt = LLVMInt32TypeInContext(g->ctx);
        if (out_type) *out_type = LLVMInt64TypeInContext(g->ctx);
        return LLVMSizeOf(lt);
    }

    case E_ARROW:
    case E_MEMBER: {
        LLVMTypeRef elem_type = NULL;
        LLVMValueRef lptr = gen_lvalue(g, e, &elem_type);
        if (lptr && elem_type) {
            if (out_type) *out_type = elem_type;
            return LLVMBuildLoad2(g->builder, elem_type, lptr, e->str);
        }
        return NULL;
    }

    case E_INDEX: {
        LLVMTypeRef elem_type = NULL;
        LLVMValueRef lptr = gen_lvalue(g, e, &elem_type);
        if (lptr && elem_type) {
            if (out_type) *out_type = elem_type;
            return LLVMBuildLoad2(g->builder, elem_type, lptr, "load_idx");
        }
        return NULL;
    }

    case E_ASSIGN: {
        LLVMTypeRef lhs_type = NULL;
        LLVMValueRef lptr = gen_lvalue(g, e->a, &lhs_type);
        if (!lptr) return NULL;
        LLVMTypeRef rhs_type = NULL;
        LLVMValueRef rval = gen_expr(g, e->b, &rhs_type);
        if (!rval) return NULL;

        const char* op = e->str;
        if (strcmp(op, "=") != 0) {
            LLVMValueRef cur_val = LLVMBuildLoad2(g->builder, lhs_type, lptr, "cur");
            LLVMTypeKind k = LLVMGetTypeKind(lhs_type);
            int is_float = (k == LLVMFloatTypeKind || k == LLVMDoubleTypeKind);
            rval = cast_to_type(g, rval, rhs_type, lhs_type);

            if (strcmp(op, "+=") == 0) rval = is_float ? LLVMBuildFAdd(g->builder, cur_val, rval, "fadd") : LLVMBuildAdd(g->builder, cur_val, rval, "add");
            else if (strcmp(op, "-=") == 0) rval = is_float ? LLVMBuildFSub(g->builder, cur_val, rval, "fsub") : LLVMBuildSub(g->builder, cur_val, rval, "sub");
            else if (strcmp(op, "*=") == 0) rval = is_float ? LLVMBuildFMul(g->builder, cur_val, rval, "fmul") : LLVMBuildMul(g->builder, cur_val, rval, "mul");
            else if (strcmp(op, "/=") == 0) rval = is_float ? LLVMBuildFDiv(g->builder, cur_val, rval, "fdiv") : LLVMBuildSDiv(g->builder, cur_val, rval, "sdiv");
        } else {
            rval = cast_to_type(g, rval, rhs_type, lhs_type);
        }
        LLVMBuildStore(g->builder, rval, lptr);
        if (out_type) *out_type = lhs_type;
        return rval;
    }

    case E_CAST: {
        LLVMTypeRef src_type = NULL;
        LLVMValueRef val = gen_expr(g, e->a, &src_type);
        LLVMTypeRef dst_type = gen_llvm_type(g, e->type);
        LLVMValueRef cast_val = cast_to_type(g, val, src_type, dst_type);
        if (out_type) *out_type = dst_type;
        return cast_val;
    }

    case E_PAREN:
        return gen_expr(g, e->a, out_type);

    case E_TERNARY: {
        LLVMValueRef cond_val = gen_expr(g, e->a, NULL);
        LLVMValueRef cond_bool = LLVMBuildICmp(g->builder, LLVMIntNE, cond_val, LLVMConstNull(LLVMTypeOf(cond_val)), "tcond");

        LLVMBasicBlockRef then_bb = LLVMAppendBasicBlockInContext(g->ctx, g->cur_fn, "tern.then");
        LLVMBasicBlockRef else_bb = LLVMAppendBasicBlockInContext(g->ctx, g->cur_fn, "tern.else");
        LLVMBasicBlockRef merge_bb = LLVMAppendBasicBlockInContext(g->ctx, g->cur_fn, "tern.end");

        LLVMBuildCondBr(g->builder, cond_bool, then_bb, else_bb);

        LLVMPositionBuilderAtEnd(g->builder, then_bb);
        LLVMTypeRef tt = NULL;
        LLVMValueRef tval = gen_expr(g, e->b, &tt);
        LLVMBasicBlockRef then_end = LLVMGetInsertBlock(g->builder);
        LLVMBuildBr(g->builder, merge_bb);

        LLVMPositionBuilderAtEnd(g->builder, else_bb);
        LLVMTypeRef et = NULL;
        LLVMValueRef eval = gen_expr(g, e->c, &et);
        LLVMBasicBlockRef else_end = LLVMGetInsertBlock(g->builder);
        LLVMBuildBr(g->builder, merge_bb);

        LLVMPositionBuilderAtEnd(g->builder, merge_bb);
        LLVMTypeRef res_type = tt ? tt : et;
        eval = cast_to_type(g, eval, et, res_type);

        LLVMValueRef phi = LLVMBuildPhi(g->builder, res_type, "tern.phi");
        LLVMValueRef incoming_vals[2] = { tval, eval };
        LLVMBasicBlockRef incoming_bbs[2] = { then_end, else_end };
        LLVMAddIncoming(phi, incoming_vals, incoming_bbs, 2);

        if (out_type) *out_type = res_type;
        return phi;
    }

    case E_NAMED_INIT: {
        const char* type_name = (e->type && e->type->name) ? e->type->name : NULL;
        const char* en = type_name ? sema_lookup_variant(g->sema, type_name) : NULL;
        if (en) {
            /* sum variant constructor: `Circle { r: 2.0 }` */
            EnumDef* ed = sema_lookup_enum(g->sema, en);
            int vi = ed ? variant_index(ed, type_name) : -1;
            if (ed && vi >= 0) {
                EnumVariant* v = &ed->variants[vi];
                AstType parent_at = { .qual = "", .name = (char*)en, .ptrs = 0 };
                LLVMTypeRef enum_t = gen_llvm_type(g, &parent_at);
                LLVMValueRef sum_alloca = LLVMBuildAlloca(g->builder, enum_t, "sum_init");
                LLVMBuildStore(g->builder, LLVMConstNull(enum_t), sum_alloca);

                LLVMValueRef tag_ptr = LLVMBuildStructGEP2(g->builder, enum_t, sum_alloca, 0, "tag_ptr");
                LLVMBuildStore(g->builder, LLVMConstInt(LLVMInt32TypeInContext(g->ctx), (unsigned long long)vi, 0), tag_ptr);

                if (v->nfields > 0) {
                    LLVMValueRef payload_ptr = LLVMBuildStructGEP2(g->builder, enum_t, sum_alloca, 1, "payload_buf");
                    LLVMTypeRef* vft = calloc(v->nfields, sizeof(LLVMTypeRef));
                    for (int k = 0; k < v->nfields; k++) vft[k] = gen_llvm_type(g, v->fields[k].type);
                    LLVMTypeRef vpayload_t = LLVMStructTypeInContext(g->ctx, vft, v->nfields, 0);

                    for (int i = 0; i < e->nnfields; i++) {
                        const char* fname = e->nfields[i].name;
                        int fidx = -1;
                        for (int j = 0; j < v->nfields; j++) {
                            if (strcmp(v->fields[j].name, fname) == 0) { fidx = j; break; }
                        }
                        if (fidx >= 0) {
                            LLVMValueRef fptr = LLVMBuildStructGEP2(g->builder, vpayload_t, payload_ptr, fidx, fname);
                            LLVMTypeRef rt = NULL;
                            LLVMValueRef fval = gen_expr(g, e->nfields[i].e, &rt);
                            fval = cast_to_type(g, fval, rt, vft[fidx]);
                            LLVMBuildStore(g->builder, fval, fptr);
                        }
                    }
                    free(vft);
                }
                if (out_type) *out_type = enum_t;
                return LLVMBuildLoad2(g->builder, enum_t, sum_alloca, "load_sum");
            }
        }

        /* Construct struct on stack */
        LLVMTypeRef st_type = gen_llvm_type(g, e->type);
        LLVMValueRef alloca_ref = LLVMBuildAlloca(g->builder, st_type, "named_init");
        StructDef* st = sema_lookup_struct(g->sema, e->type->name);
        if (st) {
            /* When struct has a parent, element index 0 is "_base". Own fields start at offset 1. */
            int base_offset = (st->parent && st->parent[0]) ? 1 : 0;
            for (int i = 0; i < e->nnfields; i++) {
                const char* fname = e->nfields[i].name;
                int fidx = -1;
                /* Search own fields first */
                for (int j = 0; j < st->nfields; j++) {
                    if (strcmp(st->fields[j].name, fname) == 0) { fidx = j; break; }
                }
                if (fidx >= 0) {
                    /* Own field: element index is fidx + base_offset */
                    LLVMValueRef fptr = LLVMBuildStructGEP2(g->builder, st_type, alloca_ref,
                                                            (unsigned)(fidx + base_offset), fname);
                    LLVMTypeRef ft = gen_llvm_type(g, st->fields[fidx].type);
                    LLVMTypeRef rt = NULL;
                    LLVMValueRef fval = gen_expr(g, e->nfields[i].e, &rt);
                    fval = cast_to_type(g, fval, rt, ft);
                    LLVMBuildStore(g->builder, fval, fptr);
                } else if (base_offset > 0) {
                    /* Try parent fields: GEP through _base (index 0) */
                    StructDef* par = sema_lookup_struct(g->sema, st->parent);
                    if (par) {
                        for (int j = 0; j < par->nfields; j++) {
                            if (strcmp(par->fields[j].name, fname) == 0) { fidx = j; break; }
                        }
                        if (fidx >= 0) {
                            LLVMTypeRef par_type = LLVMStructGetTypeAtIndex(st_type, 0);
                            LLVMValueRef base_ptr = LLVMBuildStructGEP2(g->builder, st_type, alloca_ref, 0, "_base");
                            LLVMValueRef fptr = LLVMBuildStructGEP2(g->builder, par_type, base_ptr, (unsigned)fidx, fname);
                            LLVMTypeRef ft = gen_llvm_type(g, par->fields[fidx].type);
                            LLVMTypeRef rt = NULL;
                            LLVMValueRef fval = gen_expr(g, e->nfields[i].e, &rt);
                            fval = cast_to_type(g, fval, rt, ft);
                            LLVMBuildStore(g->builder, fval, fptr);
                        }
                    }
                }
            }
        }
        if (out_type) *out_type = st_type;
        return LLVMBuildLoad2(g->builder, st_type, alloca_ref, "load_init");
    }

    case E_BRACE_INIT: {
        LLVMTypeRef st_type = out_type ? *out_type : NULL;
        AstType* at = e->type;
        StructDef* st = (at && at->name) ? sema_lookup_struct(g->sema, at->name) : NULL;
        if (!st_type && at) st_type = gen_llvm_type(g, at);
        if (!st && st_type) {
            const char* sname = LLVMGetStructName(st_type);
            if (sname) st = sema_lookup_struct(g->sema, sname);
        }
        if (st_type) {
            /* When struct has a parent, element 0 is _base; own fields start at index 1. */
            int base_offset = (st && st->parent && st->parent[0]) ? 1 : 0;
            LLVMValueRef alloca_ref = LLVMBuildAlloca(g->builder, st_type, "brace_init");
            for (int i = 0; i < e->nitems; i++) {
                unsigned elem_idx = (unsigned)(i + base_offset);
                LLVMValueRef fptr = LLVMBuildStructGEP2(g->builder, st_type, alloca_ref, elem_idx, "f");
                LLVMTypeRef ft = (st && i < st->nfields) ? gen_llvm_type(g, st->fields[i].type)
                                                         : LLVMInt32TypeInContext(g->ctx);
                LLVMTypeRef rt = NULL;
                LLVMValueRef fval = gen_expr(g, e->items[i], &rt);
                fval = cast_to_type(g, fval, rt, ft);
                LLVMBuildStore(g->builder, fval, fptr);
            }
            if (out_type) *out_type = st_type;
            return LLVMBuildLoad2(g->builder, st_type, alloca_ref, "load_brace");
        }
        return NULL;
    }

    case E_COMPOUND: {
        LLVMTypeRef st_type = e->type ? gen_llvm_type(g, e->type) : (out_type ? *out_type : NULL);
        if (!st_type) st_type = LLVMInt32TypeInContext(g->ctx);
        LLVMValueRef alloca_ref = LLVMBuildAlloca(g->builder, st_type, "compound_init");
        StructDef* st = (e->type && e->type->name) ? sema_lookup_struct(g->sema, e->type->name) : NULL;
        /* When struct has a parent, element 0 is _base; own fields start at index 1. */
        int base_offset = (st && st->parent && st->parent[0]) ? 1 : 0;
        int pos_idx = 0;
        for (int i = 0; i < e->ncitems; i++) {
            int fidx = -1;
            if (e->citems[i].name && st) {
                for (int j = 0; j < st->nfields; j++) {
                    if (strcmp(st->fields[j].name, e->citems[i].name) == 0) { fidx = j; break; }
                }
            } else {
                fidx = pos_idx++;
            }
            if (fidx >= 0 && (!st || fidx < st->nfields)) {
                unsigned elem_idx = (unsigned)(fidx + base_offset);
                LLVMValueRef fptr = LLVMBuildStructGEP2(g->builder, st_type, alloca_ref, elem_idx, "comp_f");
                LLVMTypeRef ft = (st && fidx < st->nfields) ? gen_llvm_type(g, st->fields[fidx].type) : LLVMInt32TypeInContext(g->ctx);
                LLVMTypeRef rt = NULL;
                LLVMValueRef fval = gen_expr(g, e->citems[i].e, &rt);
                if (fval) {
                    fval = cast_to_type(g, fval, rt, ft);
                    LLVMBuildStore(g->builder, fval, fptr);
                }
            }
        }
        if (out_type) *out_type = st_type;
        return LLVMBuildLoad2(g->builder, st_type, alloca_ref, "load_compound");
    }

    case E_ARR_LIT: {
        LLVMTypeRef elem_type = NULL;
        if (out_type && *out_type && LLVMGetTypeKind(*out_type) == LLVMArrayTypeKind) {
            elem_type = LLVMGetElementType(*out_type);
        }
        if (!elem_type && e->nitems > 0) {
            gen_expr(g, e->items[0], &elem_type);
        }
        if (!elem_type) elem_type = LLVMInt32TypeInContext(g->ctx);

        LLVMTypeRef arr_type = LLVMArrayType(elem_type, (unsigned)e->nitems);
        LLVMValueRef alloca_ref = LLVMBuildAlloca(g->builder, arr_type, "arr_lit");
        for (int i = 0; i < e->nitems; i++) {
            LLVMValueRef idx_vals[2] = {
                LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0),
                LLVMConstInt(LLVMInt32TypeInContext(g->ctx), (unsigned long long)i, 0)
            };
            LLVMValueRef elem_ptr = LLVMBuildGEP2(g->builder, arr_type, alloca_ref, idx_vals, 2, "arr_elem");
            LLVMTypeRef it_type = NULL;
            LLVMValueRef it_val = gen_expr(g, e->items[i], &it_type);
            if (it_val) {
                it_val = cast_to_type(g, it_val, it_type, elem_type);
                LLVMBuildStore(g->builder, it_val, elem_ptr);
            }
        }
        if (out_type) *out_type = arr_type;
        return LLVMBuildLoad2(g->builder, arr_type, alloca_ref, "load_arr_lit");
    }

    case E_QUESTION:
        return gen_expr(g, e->a, out_type);

    case E_RANGE:
        return gen_expr(g, e->a, out_type);

    case E_MATCH:
        return gen_match(g, e->a, e->marms, e->nmarms, e->type, out_type);

    default:
        break;
    }

    return NULL;
}

static LLVMValueRef gen_match(LLVMGen* g, Expr* scrut_expr, MatchArm* marms, int nmarms, AstType* result_ast_type, LLVMTypeRef* out_type) {
    if (!g || !scrut_expr) return NULL;

    AstType* st = llvm_resolve_expr_type(g, scrut_expr);
    int free_st = 0;
    if (!st && scrut_expr->type) st = scrut_expr->type;

    EnumDef* ed = (st && st->name) ? sema_lookup_enum(g->sema, st->name) : NULL;
    if (!ed) {
        for (int i = 0; i < nmarms; i++) {
            MatchArm* arm = &marms[i];
            Expr* p = arm->pattern;
            const char* vname = NULL;
            if (p) {
                if (p->kind == E_IDENT && strcmp(p->str, "_") != 0) vname = p->str;
                else if (p->kind == E_CALL && p->a && p->a->kind == E_IDENT) vname = p->a->str;
                else if (p->kind == E_NAMED_INIT && p->type) vname = p->type->name;
            }
            if (vname) {
                const char* ename = sema_lookup_variant(g->sema, vname);
                if (ename) {
                    ed = sema_lookup_enum(g->sema, ename);
                    if (ed) break;
                }
            }
        }
    }
    int is_sum = (ed && enum_has_payload(ed));

    LLVMTypeRef res_t = result_ast_type ? gen_llvm_type(g, result_ast_type) : NULL;
    if (!res_t && out_type && *out_type) res_t = *out_type;
    if (!res_t && g->cur_ret_type) res_t = g->cur_ret_type;
    int is_void = !res_t || (LLVMGetTypeKind(res_t) == LLVMVoidTypeKind);
    LLVMValueRef res_slot = (!is_void && res_t) ? LLVMBuildAlloca(g->builder, res_t, "match_res") : NULL;

    LLVMBasicBlockRef exit_bb = LLVMAppendBasicBlockInContext(g->ctx, g->cur_fn, "match.exit");

    if (is_sum) {
        AstType enum_at = { .qual = "", .name = ed->name, .ptrs = 0 };
        LLVMTypeRef sum_t = gen_llvm_type(g, &enum_at);
        LLVMTypeRef lval_t = NULL;
        LLVMValueRef sum_slot = gen_lvalue(g, scrut_expr, &lval_t);
        if (!sum_slot) {
            LLVMValueRef sum_val = gen_expr(g, scrut_expr, NULL);
            sum_slot = LLVMBuildAlloca(g->builder, sum_t, "match_scrut");
            LLVMBuildStore(g->builder, sum_val, sum_slot);
        }

        LLVMValueRef tag_ptr = LLVMBuildStructGEP2(g->builder, sum_t, sum_slot, 0, "tag_ptr");
        LLVMValueRef tag_val = LLVMBuildLoad2(g->builder, LLVMInt32TypeInContext(g->ctx), tag_ptr, "tag");
        LLVMValueRef payload_ptr = LLVMBuildStructGEP2(g->builder, sum_t, sum_slot, 1, "payload_buf");

        for (int i = 0; i < nmarms; i++) {
            MatchArm* arm = &marms[i];
            Expr* p = arm->pattern;
            int is_wild = (p && p->kind == E_IDENT && strcmp(p->str, "_") == 0);

            if (is_wild) {
                LLVMBasicBlockRef arm_bb = LLVMAppendBasicBlockInContext(g->ctx, g->cur_fn, "match.wild");
                LLVMBuildBr(g->builder, arm_bb);
                LLVMPositionBuilderAtEnd(g->builder, arm_bb);

                LLVMTypeRef bt = NULL;
                LLVMValueRef bval = gen_expr(g, arm->body, &bt);
                if (res_slot && bval) {
                    bval = cast_to_type(g, bval, bt, res_t);
                    LLVMBuildStore(g->builder, bval, res_slot);
                }
                if (!is_block_terminated(g)) LLVMBuildBr(g->builder, exit_bb);
                break;
            } else {
                const char* vname = NULL;
                if (p->kind == E_IDENT) vname = p->str;
                else if (p->kind == E_CALL && p->a && p->a->kind == E_IDENT) vname = p->a->str;
                else if (p->kind == E_NAMED_INIT && p->type) vname = p->type->name;

                int vi = vname ? variant_index(ed, vname) : -1;
                EnumVariant* v = (vi >= 0) ? &ed->variants[vi] : NULL;

                LLVMBasicBlockRef arm_bb = LLVMAppendBasicBlockInContext(g->ctx, g->cur_fn, "match.arm");
                LLVMBasicBlockRef next_bb = LLVMAppendBasicBlockInContext(g->ctx, g->cur_fn, "match.next");

                LLVMValueRef cmp = LLVMBuildICmp(g->builder, LLVMIntEQ, tag_val, LLVMConstInt(LLVMInt32TypeInContext(g->ctx), (unsigned long long)vi, 0), "tag_eq");
                LLVMBuildCondBr(g->builder, cmp, arm_bb, next_bb);

                LLVMPositionBuilderAtEnd(g->builder, arm_bb);
                int save_locals = g->nlocals;

                if (v && v->nfields > 0) {
                    LLVMTypeRef* vft = calloc(v->nfields, sizeof(LLVMTypeRef));
                    for (int k = 0; k < v->nfields; k++) vft[k] = gen_llvm_type(g, v->fields[k].type);
                    LLVMTypeRef vpayload_t = LLVMStructTypeInContext(g->ctx, vft, v->nfields, 0);

                    if (p->kind == E_NAMED_INIT) {
                        for (int k = 0; k < p->nnfields; k++) {
                            const char* fname = p->nfields[k].name;
                            for (int m = 0; m < v->nfields; m++) {
                                if (strcmp(v->fields[m].name, fname) == 0) {
                                    const char* bname = (p->nfields[k].e && p->nfields[k].e->kind == E_IDENT) ? p->nfields[k].e->str : fname;
                                    LLVMValueRef fptr = LLVMBuildStructGEP2(g->builder, vpayload_t, payload_ptr, m, bname);
                                    LLVMValueRef fval = LLVMBuildLoad2(g->builder, vft[m], fptr, bname);
                                    LLVMValueRef lslot = LLVMBuildAlloca(g->builder, vft[m], bname);
                                    LLVMBuildStore(g->builder, fval, lslot);
                                    gen_add_local(g, bname, lslot, vft[m], v->fields[m].type);
                                    break;
                                }
                            }
                        }
                    } else if (p->kind == E_CALL) {
                        for (int k = 0; k < p->nitems && k < v->nfields; k++) {
                            if (p->items[k]->kind == E_IDENT && strcmp(p->items[k]->str, "_") != 0) {
                                const char* bname = p->items[k]->str;
                                LLVMValueRef fptr = LLVMBuildStructGEP2(g->builder, vpayload_t, payload_ptr, k, bname);
                                LLVMValueRef fval = LLVMBuildLoad2(g->builder, vft[k], fptr, bname);
                                LLVMValueRef lslot = LLVMBuildAlloca(g->builder, vft[k], bname);
                                LLVMBuildStore(g->builder, fval, lslot);
                                gen_add_local(g, bname, lslot, vft[k], v->fields[k].type);
                            }
                        }
                    }
                    free(vft);
                }

                LLVMTypeRef bt = NULL;
                LLVMValueRef bval = gen_expr(g, arm->body, &bt);
                if (res_slot && bval) {
                    bval = cast_to_type(g, bval, bt, res_t);
                    LLVMBuildStore(g->builder, bval, res_slot);
                }
                g->nlocals = save_locals;
                if (!is_block_terminated(g)) LLVMBuildBr(g->builder, exit_bb);
                LLVMPositionBuilderAtEnd(g->builder, next_bb);
            }
        }
        if (!is_block_terminated(g)) LLVMBuildBr(g->builder, exit_bb);
    } else {
        /* Unit enum or integer scalar match */
        LLVMTypeRef ct = NULL;
        LLVMValueRef cond_val = gen_expr(g, scrut_expr, &ct);
        if (!ct) ct = LLVMInt32TypeInContext(g->ctx);

        int def_idx = -1;
        for (int i = 0; i < nmarms; i++) {
            if (marms[i].pattern && marms[i].pattern->kind == E_IDENT && strcmp(marms[i].pattern->str, "_") == 0) {
                def_idx = i;
                break;
            }
        }

        LLVMValueRef* case_vals = calloc(nmarms > 0 ? nmarms : 1, sizeof(LLVMValueRef));
        for (int i = 0; i < nmarms; i++) {
            if (i == def_idx) continue;
            case_vals[i] = gen_expr(g, marms[i].pattern, NULL);
        }

        LLVMBasicBlockRef default_bb = (def_idx >= 0) ? LLVMAppendBasicBlockInContext(g->ctx, g->cur_fn, "match.default") : exit_bb;
        LLVMValueRef sw = LLVMBuildSwitch(g->builder, cond_val, default_bb, nmarms);

        for (int i = 0; i < nmarms; i++) {
            MatchArm* arm = &marms[i];
            if (i == def_idx) continue;
            LLVMBasicBlockRef case_bb = LLVMAppendBasicBlockInContext(g->ctx, g->cur_fn, "match.case");
            LLVMAddCase(sw, case_vals[i], case_bb);
            LLVMPositionBuilderAtEnd(g->builder, case_bb);
            LLVMTypeRef bt = NULL;
            LLVMValueRef bval = gen_expr(g, arm->body, &bt);
            if (res_slot && bval) {
                bval = cast_to_type(g, bval, bt, res_t);
                LLVMBuildStore(g->builder, bval, res_slot);
            }
            if (!is_block_terminated(g)) LLVMBuildBr(g->builder, exit_bb);
        }
        free(case_vals);

        if (def_idx >= 0) {
            LLVMPositionBuilderAtEnd(g->builder, default_bb);
            LLVMTypeRef bt = NULL;
            LLVMValueRef bval = gen_expr(g, marms[def_idx].body, &bt);
            if (res_slot && bval) {
                bval = cast_to_type(g, bval, bt, res_t);
                LLVMBuildStore(g->builder, bval, res_slot);
            }
            if (!is_block_terminated(g)) LLVMBuildBr(g->builder, exit_bb);
        }
    }

    if (free_st && st) free(st);
    LLVMPositionBuilderAtEnd(g->builder, exit_bb);
    if (res_slot) {
        if (out_type) *out_type = res_t;
        return LLVMBuildLoad2(g->builder, res_t, res_slot, "match_res");
    }
    return NULL;
}

/* Evaluate a constant integer expression (E_LITERAL, unary minus, E_BINARY with +, -, multiply, divide)
   at codegen time. Returns the computed value, or 1 on failure (safe default for array dimensions). */
static long eval_const_expr(Expr* e) {
    if (!e) return 1;
    if (e->kind == E_LITERAL && e->str) {
        long v = atol(e->str);
        return v > 0 ? v : 1;
    }
    if (e->kind == E_UNARY && e->str && strcmp(e->str, "-") == 0 && e->a) {
        return -eval_const_expr(e->a);
    }
    if (e->kind == E_PAREN && e->a) {
        return eval_const_expr(e->a);
    }
    if (e->kind == E_BINARY && e->str && e->a && e->b) {
        long lhs = eval_const_expr(e->a);
        long rhs = eval_const_expr(e->b);
        if (strcmp(e->str, "+") == 0) return lhs + rhs;
        if (strcmp(e->str, "-") == 0) return lhs - rhs;
        if (strcmp(e->str, "*") == 0) return lhs * rhs;
        if (strcmp(e->str, "/") == 0 && rhs != 0) return lhs / rhs;
    }
    return 1;
}

static void gen_stmt(LLVMGen* g, Stmt* s) {
    if (!s || is_block_terminated(g)) return;

    switch (s->kind) {
    case S_EXPR:
        gen_expr(g, s->e, NULL);
        break;

    case S_DECL: {
        Decl* d = s->decl;
        if (!d) break;
        AstType* at = d->type;
        if (!at && d->init) {
            at = sema_resolve_type(g->sema, d->init);
        }
        LLVMTypeRef vt = gen_llvm_type(g, at);
        if (d->dim) {
            long dim_sz = eval_const_expr(d->dim);
            if (dim_sz <= 0) dim_sz = 1;
            vt = LLVMArrayType(vt, (unsigned)dim_sz);
        }
        LLVMValueRef alloca_ref = LLVMBuildAlloca(g->builder, vt, d->name);
        gen_add_local(g, d->name, alloca_ref, vt, at);

        if (d->init) {
            LLVMTypeRef rt = vt;
            LLVMValueRef init_val = gen_expr(g, d->init, &rt);
            if (init_val) {
                init_val = cast_to_type(g, init_val, rt, vt);
                LLVMBuildStore(g->builder, init_val, alloca_ref);
            }
        }
        break;
    }

    case S_BLOCK: {
        gen_push_defer_frame(g);
        for (int i = 0; i < s->nstmts; i++) {
            gen_stmt(g, s->stmts[i]);
            if (is_block_terminated(g)) break;
        }
        gen_pop_defer_frame(g);
        break;
    }

    case S_IF: {
        LLVMValueRef cond_val = gen_expr(g, s->cond, NULL);
        LLVMValueRef cond_bool = LLVMBuildICmp(g->builder, LLVMIntNE, cond_val, LLVMConstNull(LLVMTypeOf(cond_val)), "if.cond");

        LLVMBasicBlockRef then_bb = LLVMAppendBasicBlockInContext(g->ctx, g->cur_fn, "if.then");
        LLVMBasicBlockRef else_bb = s->els ? LLVMAppendBasicBlockInContext(g->ctx, g->cur_fn, "if.else") : NULL;
        LLVMBasicBlockRef merge_bb = LLVMAppendBasicBlockInContext(g->ctx, g->cur_fn, "if.end");

        LLVMBuildCondBr(g->builder, cond_bool, then_bb, else_bb ? else_bb : merge_bb);

        LLVMPositionBuilderAtEnd(g->builder, then_bb);
        gen_stmt(g, s->then);
        if (!is_block_terminated(g)) {
            LLVMBuildBr(g->builder, merge_bb);
        }

        if (else_bb) {
            LLVMPositionBuilderAtEnd(g->builder, else_bb);
            gen_stmt(g, s->els);
            if (!is_block_terminated(g)) {
                LLVMBuildBr(g->builder, merge_bb);
            }
        }

        LLVMPositionBuilderAtEnd(g->builder, merge_bb);
        break;
    }

    case S_WHILE: {
        LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(g->ctx, g->cur_fn, "while.cond");
        LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(g->ctx, g->cur_fn, "while.body");
        LLVMBasicBlockRef exit_bb = LLVMAppendBasicBlockInContext(g->ctx, g->cur_fn, "while.end");

        LLVMBuildBr(g->builder, cond_bb);

        LLVMPositionBuilderAtEnd(g->builder, cond_bb);
        LLVMValueRef cond_val = gen_expr(g, s->cond, NULL);
        LLVMValueRef cond_bool = LLVMBuildICmp(g->builder, LLVMIntNE, cond_val, LLVMConstNull(LLVMTypeOf(cond_val)), "wcond");
        LLVMBuildCondBr(g->builder, cond_bool, body_bb, exit_bb);

        LLVMPositionBuilderAtEnd(g->builder, body_bb);
        if (g->loop_depth < 32) {
            g->loop_cond_bbs[g->loop_depth] = cond_bb;
            g->loop_exit_bbs[g->loop_depth] = exit_bb;
            g->loop_depth++;
        }
        gen_stmt(g, s->body);
        if (g->loop_depth > 0) g->loop_depth--;
        if (!is_block_terminated(g)) {
            LLVMBuildBr(g->builder, cond_bb);
        }

        LLVMPositionBuilderAtEnd(g->builder, exit_bb);
        break;
    }

    case S_FOR: {
        if (s->init_decl) {
            Stmt ds = { .kind = S_DECL, .decl = s->init_decl };
            gen_stmt(g, &ds);
        } else if (s->init_expr) {
            gen_expr(g, s->init_expr, NULL);
        }

        LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(g->ctx, g->cur_fn, "for.cond");
        LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(g->ctx, g->cur_fn, "for.body");
        LLVMBasicBlockRef step_bb = LLVMAppendBasicBlockInContext(g->ctx, g->cur_fn, "for.step");
        LLVMBasicBlockRef exit_bb = LLVMAppendBasicBlockInContext(g->ctx, g->cur_fn, "for.end");

        LLVMBuildBr(g->builder, cond_bb);

        LLVMPositionBuilderAtEnd(g->builder, cond_bb);
        if (s->cond) {
            LLVMValueRef cond_val = gen_expr(g, s->cond, NULL);
            LLVMValueRef cond_bool = LLVMBuildICmp(g->builder, LLVMIntNE, cond_val, LLVMConstNull(LLVMTypeOf(cond_val)), "fcond");
            LLVMBuildCondBr(g->builder, cond_bool, body_bb, exit_bb);
        } else {
            LLVMBuildBr(g->builder, body_bb);
        }

        LLVMPositionBuilderAtEnd(g->builder, body_bb);
        if (g->loop_depth < 32) {
            g->loop_cond_bbs[g->loop_depth] = step_bb;
            g->loop_exit_bbs[g->loop_depth] = exit_bb;
            g->loop_depth++;
        }
        gen_stmt(g, s->body);
        if (g->loop_depth > 0) g->loop_depth--;
        if (!is_block_terminated(g)) {
            LLVMBuildBr(g->builder, step_bb);
        }

        LLVMPositionBuilderAtEnd(g->builder, step_bb);
        if (s->step) {
            gen_expr(g, s->step, NULL);
        }
        LLVMBuildBr(g->builder, cond_bb);

        LLVMPositionBuilderAtEnd(g->builder, exit_bb);
        break;
    }

    case S_DEFER:
        gen_add_defer(g, s->defer ? s->defer : s->body);
        break;

    case S_RETURN: {
        LLVMValueRef ret_val = NULL;
        if (s->e) {
            LLVMTypeRef rt = NULL;
            ret_val = gen_expr(g, s->e, &rt);
            ret_val = cast_to_type(g, ret_val, rt, g->cur_ret_type);
        }
        gen_flush_all_defers(g);
        if (ret_val) {
            LLVMBuildRet(g->builder, ret_val);
        } else {
            LLVMBuildRetVoid(g->builder);
        }
        break;
    }

    case S_BREAK:
        if (g->loop_depth > 0) {
            gen_flush_loop_defers(g);
            LLVMBuildBr(g->builder, g->loop_exit_bbs[g->loop_depth - 1]);
        }
        break;

    case S_CONTINUE:
        if (g->loop_depth > 0) {
            gen_flush_loop_defers(g);
            LLVMBuildBr(g->builder, g->loop_cond_bbs[g->loop_depth - 1]);
        }
        break;

    case S_SWITCH: {
        LLVMValueRef cond_val = gen_expr(g, s->e, NULL);
        LLVMBasicBlockRef exit_bb = LLVMAppendBasicBlockInContext(g->ctx, g->cur_fn, "sw.end");
        LLVMBasicBlockRef default_bb = exit_bb;

        int def_idx = -1;
        int case_count = 0;
        for (int i = 0; i < s->narms; i++) {
            if (s->arms[i].is_default) {
                def_idx = i;
            } else {
                case_count += s->arms[i].nlabels;
            }
        }

        if (def_idx >= 0) {
            default_bb = LLVMAppendBasicBlockInContext(g->ctx, g->cur_fn, "sw.default");
        }

        LLVMValueRef sw = LLVMBuildSwitch(g->builder, cond_val, default_bb, case_count);

        if (g->loop_depth < 32) {
            g->loop_cond_bbs[g->loop_depth] = exit_bb;
            g->loop_exit_bbs[g->loop_depth] = exit_bb;
            g->loop_depth++;
        }

        for (int i = 0; i < s->narms; i++) {
            SwitchArm* arm = &s->arms[i];
            LLVMBasicBlockRef arm_bb;
            if (arm->is_default) {
                arm_bb = default_bb;
            } else {
                arm_bb = LLVMAppendBasicBlockInContext(g->ctx, g->cur_fn, "sw.case");
                for (int j = 0; j < arm->nlabels; j++) {
                    LLVMTypeRef lt = NULL;
                    LLVMValueRef lval = gen_expr(g, arm->labels[j], &lt);
                    LLVMAddCase(sw, lval, arm_bb);
                }
            }
            LLVMPositionBuilderAtEnd(g->builder, arm_bb);
            gen_stmt(g, arm->body);
            if (!is_block_terminated(g)) {
                LLVMBuildBr(g->builder, exit_bb);
            }
        }

        if (g->loop_depth > 0) g->loop_depth--;
        LLVMPositionBuilderAtEnd(g->builder, exit_bb);
        break;
    }

    case S_MATCH: {
        gen_match(g, s->e, s->marms, s->nmarms, NULL, NULL);
        break;
    }

    case S_FORIN: {
        if (s->iter && s->iter->kind == E_ARR_LIT) {
            int nitems = s->iter->nitems;
            LLVMTypeRef elem_type = LLVMInt32TypeInContext(g->ctx);
            if (nitems > 0) {
                LLVMTypeRef it_t = NULL;
                gen_expr(g, s->iter->items[0], &it_t);
                if (it_t) elem_type = it_t;
            }
            LLVMTypeRef arr_type = LLVMArrayType(elem_type, nitems > 0 ? (unsigned)nitems : 1);
            LLVMValueRef arr_alloca = LLVMBuildAlloca(g->builder, arr_type, "forin_arr");

            for (int i = 0; i < nitems; i++) {
                LLVMValueRef idx_vals[2] = {
                    LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0),
                    LLVMConstInt(LLVMInt32TypeInContext(g->ctx), (unsigned long long)i, 0)
                };
                LLVMValueRef elem_ptr = LLVMBuildGEP2(g->builder, arr_type, arr_alloca, idx_vals, 2, "elem_ptr");
                LLVMTypeRef rt = NULL;
                LLVMValueRef val = gen_expr(g, s->iter->items[i], &rt);
                val = cast_to_type(g, val, rt, elem_type);
                LLVMBuildStore(g->builder, val, elem_ptr);
            }

            LLVMValueRef i_alloca = LLVMBuildAlloca(g->builder, LLVMInt32TypeInContext(g->ctx), "__i");
            LLVMBuildStore(g->builder, LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0), i_alloca);

            LLVMValueRef var_alloca = LLVMBuildAlloca(g->builder, elem_type, s->var);
            gen_add_local(g, s->var, var_alloca, elem_type, NULL);

            LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(g->ctx, g->cur_fn, "forin.cond");
            LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(g->ctx, g->cur_fn, "forin.body");
            LLVMBasicBlockRef step_bb = LLVMAppendBasicBlockInContext(g->ctx, g->cur_fn, "forin.step");
            LLVMBasicBlockRef exit_bb = LLVMAppendBasicBlockInContext(g->ctx, g->cur_fn, "forin.end");

            LLVMBuildBr(g->builder, cond_bb);

            LLVMPositionBuilderAtEnd(g->builder, cond_bb);
            LLVMValueRef cur_i = LLVMBuildLoad2(g->builder, LLVMInt32TypeInContext(g->ctx), i_alloca, "i_val");
            LLVMValueRef cmp = LLVMBuildICmp(g->builder, LLVMIntSLT, cur_i, LLVMConstInt(LLVMInt32TypeInContext(g->ctx), (unsigned long long)nitems, 0), "cmp");
            LLVMBuildCondBr(g->builder, cmp, body_bb, exit_bb);

            LLVMPositionBuilderAtEnd(g->builder, body_bb);
            LLVMValueRef cur_body_i = LLVMBuildLoad2(g->builder, LLVMInt32TypeInContext(g->ctx), i_alloca, "i_cur");
            LLVMValueRef idx_body[2] = {
                LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0),
                cur_body_i
            };
            LLVMValueRef e_ptr = LLVMBuildGEP2(g->builder, arr_type, arr_alloca, idx_body, 2, "cur_elem_ptr");
            LLVMValueRef e_val = LLVMBuildLoad2(g->builder, elem_type, e_ptr, "cur_elem");
            LLVMBuildStore(g->builder, e_val, var_alloca);

            if (g->loop_depth < 32) {
                g->loop_cond_bbs[g->loop_depth] = step_bb;
                g->loop_exit_bbs[g->loop_depth] = exit_bb;
                g->loop_depth++;
            }
            gen_stmt(g, s->body);
            if (g->loop_depth > 0) g->loop_depth--;
            if (!is_block_terminated(g)) {
                LLVMBuildBr(g->builder, step_bb);
            }

            LLVMPositionBuilderAtEnd(g->builder, step_bb);
            LLVMValueRef cur_step_i = LLVMBuildLoad2(g->builder, LLVMInt32TypeInContext(g->ctx), i_alloca, "i_step");
            LLVMValueRef next_i = LLVMBuildAdd(g->builder, cur_step_i, LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 1, 0), "next_i");
            LLVMBuildStore(g->builder, next_i, i_alloca);
            LLVMBuildBr(g->builder, cond_bb);

            LLVMPositionBuilderAtEnd(g->builder, exit_bb);
        }
        break;
    }

    case S_EMPTY:
        break;

    default:
        break;
    }
}

static void gen_function_body(LLVMGen* g, FnDef* fn, const char* mangled_name) {
    if (!fn || !fn->body) return;
    const char* name = mangled_name ? mangled_name : fn->name;
    LLVMValueRef fn_val = LLVMGetNamedFunction(g->module, name);
    if (!fn_val) return;

    g->cur_fn = fn_val;
    g->cur_ast_ret = fn->ret;
    g->cur_ret_type = gen_llvm_type(g, fn->ret);
    g->nlocals = 0;
    g->defer_depth = 0;
    g->loop_depth = 0;

    LLVMBasicBlockRef entry_bb = LLVMAppendBasicBlockInContext(g->ctx, fn_val, "entry");
    LLVMPositionBuilderAtEnd(g->builder, entry_bb);

    int has_self = (fn->nparams > 0 && strcmp(fn->params[0].name, "self") == 0);
    int p_offset = 0;
    if (mangled_name && !has_self) {
        LLVMValueRef self_param = LLVMGetParam(fn_val, 0);
        LLVMValueRef alloca_ref = LLVMBuildAlloca(g->builder, LLVMPointerTypeInContext(g->ctx, 0), "self");
        LLVMBuildStore(g->builder, self_param, alloca_ref);
        gen_add_local(g, "self", alloca_ref, LLVMPointerTypeInContext(g->ctx, 0), NULL);
        p_offset = 1;
    }

    for (int i = 0; i < fn->nparams; i++) {
        Param* p = &fn->params[i];
        LLVMTypeRef pt = gen_llvm_type(g, p->type);
        LLVMValueRef param_val = LLVMGetParam(fn_val, i + p_offset);
        LLVMValueRef alloca_ref = LLVMBuildAlloca(g->builder, pt, p->name);
        LLVMBuildStore(g->builder, param_val, alloca_ref);
        gen_add_local(g, p->name, alloca_ref, pt, p->type);
    }

    gen_stmt(g, fn->body);

    if (!is_block_terminated(g)) {
        gen_flush_all_defers(g);
        if (LLVMGetTypeKind(g->cur_ret_type) == LLVMVoidTypeKind) {
            LLVMBuildRetVoid(g->builder);
        } else if (strcmp(name, "main") == 0) {
            LLVMBuildRet(g->builder, LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0));
        } else {
            LLVMBuildUnreachable(g->builder);
        }
    }
}

static void llvm_backend_destroy(Backend* b) {
    free(b);
}

static void llvm_backend_compile_and_link_raw_c(LLVMGen* g, Program* prog) {
    if (!g || !prog) return;

    size_t total_len = 0;
    int has_defs = 0;
    for (int i = 0; i < prog->nitems; i++) {
        if (prog->items[i]->kind == TOP_RAW && prog->items[i]->raw_len > 0) {
            total_len += (size_t)prog->items[i]->raw_len + 2;
            const char* r = prog->items[i]->raw;
            int rlen = prog->items[i]->raw_len;
            const char* end = r + rlen;
            while (r < end) {
                while (r < end && (*r == ' ' || *r == '\t' || *r == '\r' || *r == '\n')) r++;
                if (r >= end) break;
                if (*r != '#') {
                    has_defs = 1;
                    break;
                }
                const char* nl = memchr(r, '\n', end - r);
                r = nl ? nl + 1 : end;
            }
        }
    }

    if (!has_defs || total_len == 0) return;

    char* raw_c = malloc(total_len + 1);
    if (!raw_c) return;
    char* cur = raw_c;
    for (int i = 0; i < prog->nitems; i++) {
        if (prog->items[i]->kind == TOP_RAW && prog->items[i]->raw_len > 0) {
            memcpy(cur, prog->items[i]->raw, (size_t)prog->items[i]->raw_len);
            cur += prog->items[i]->raw_len;
            *cur++ = '\n';
        }
    }
    *cur = '\0';

    char c_tmp[] = "/tmp/rk_raw_XXXXXX.c";
    int fd = mkstemps(c_tmp, 2);
    if (fd < 0) { free(raw_c); return; }
    FILE* fc = fdopen(fd, "w");
    if (fc) {
        fputs(raw_c, fc);
        fclose(fc);
    }
    free(raw_c);

    char ll_tmp[sizeof(c_tmp) + 4];
    snprintf(ll_tmp, sizeof(ll_tmp), "%.*s.ll", (int)(strlen(c_tmp) - 2), c_tmp);

    size_t cmd_len = 512 + strlen(c_tmp) + strlen(ll_tmp);
    if (g->sema && g->sema->include_dirs) {
        for (size_t i = 0; i < g->sema->n_include_dirs; i++) {
            if (g->sema->include_dirs[i]) {
                cmd_len += strlen(g->sema->include_dirs[i]) + 8;
            }
        }
    }
    char* cmd = (char*)malloc(cmd_len);
    if (!cmd) { remove(c_tmp); return; }

    int n = snprintf(cmd, cmd_len, "clang -Dstatic= -S -emit-llvm -O0 -w -x c \"%s\" -o \"%s\"", c_tmp, ll_tmp);
    if (g->sema && g->sema->include_dirs) {
        for (size_t i = 0; i < g->sema->n_include_dirs; i++) {
            if (g->sema->include_dirs[i]) {
                n += snprintf(cmd + n, cmd_len - (size_t)n, " -I\"%s\"", g->sema->include_dirs[i]);
            }
        }
    }
    snprintf(cmd + n, cmd_len - (size_t)n, " 2>/dev/null");

    int ret = system(cmd);
    free(cmd);
    remove(c_tmp);

    if (ret == 0) {
        int lsz = 0;
        char* ll_content = util_read_file(ll_tmp, &lsz);
        remove(ll_tmp);
        if (ll_content && lsz > 0) {
            LLVMMemoryBufferRef mem = LLVMCreateMemoryBufferWithMemoryRange(ll_content, (size_t)lsz, "raw_c.ll", 0);
            LLVMModuleRef raw_mod = NULL;
            char* parse_err = NULL;
            if (LLVMParseIRInContext2(g->ctx, mem, &raw_mod, &parse_err) == 0 && raw_mod) {
                /* Promote any static/internal linkage to external so LLVMLinkModules2 links them */
                for (LLVMValueRef fn = LLVMGetFirstFunction(raw_mod); fn; fn = LLVMGetNextFunction(fn)) {
                    if (LLVMGetLinkage(fn) == LLVMInternalLinkage) {
                        LLVMSetLinkage(fn, LLVMExternalLinkage);
                    }
                }
                for (LLVMValueRef gv = LLVMGetFirstGlobal(raw_mod); gv; gv = LLVMGetNextGlobal(gv)) {
                    if (LLVMGetLinkage(gv) == LLVMInternalLinkage) {
                        LLVMSetLinkage(gv, LLVMExternalLinkage);
                    }
                }
                LLVMLinkModules2(g->module, raw_mod);
            }
            if (parse_err) LLVMDisposeMessage(parse_err);
            LLVMDisposeMemoryBuffer(mem);
            free(ll_content);
        }
    }
}

static LLVMModuleRef llvm_backend_build_module(LLVMContextRef ctx, Sema* sema, Program* prog) {
    LLVMModuleRef module = LLVMModuleCreateWithNameInContext("rook_module", ctx);
    LLVMSetSourceFileName(module, "rook_source.rook", 16);

    LLVMBuilderRef builder = LLVMCreateBuilderInContext(ctx);

    LLVMGen g = {
        .ctx = ctx,
        .module = module,
        .builder = builder,
        .sema = sema,
        .prog = prog,
    };

    llvm_backend_compile_and_link_raw_c(&g, prog);

    /* Pass 1: Declare all structs and enums */
    for (int i = 0; i < prog->nitems; i++) {
        Item* it = prog->items[i];
        if (it->kind == TOP_STRUCT && it->st) {
            gen_llvm_type(&g, sema_mk_type("", it->st->name, 0));
        } else if (it->kind == TOP_ENUM && it->ed) {
            gen_llvm_type(&g, sema_mk_type("", it->ed->name, 0));
        }
    }

    /* Pass 2: Declare all function prototypes */
    for (int i = 0; i < prog->nitems; i++) {
        Item* it = prog->items[i];
        if (it->kind == TOP_FN && it->fn) {
            FnDef* fn = it->fn;
            if (LLVMGetNamedFunction(module, fn->name)) {
                continue;
            }
            LLVMTypeRef ret_t = gen_llvm_type(&g, fn->ret);
            LLVMTypeRef* pts = calloc(fn->nparams > 0 ? fn->nparams : 1, sizeof(LLVMTypeRef));
            for (int p = 0; p < fn->nparams; p++) {
                pts[p] = gen_llvm_type(&g, fn->params[p].type);
            }
            LLVMTypeRef fn_t = LLVMFunctionType(ret_t, pts, fn->nparams, 0);
            LLVMAddFunction(module, fn->name, fn_t);
            free(pts);
        } else if (it->kind == TOP_IMPL && it->im) {
            ImplDef* im = it->im;
            for (int m = 0; m < im->nmethods; m++) {
                FnDef* fn = im->methods[m];
                char mname[256];
                snprintf(mname, sizeof(mname), "%s_%s", im->target->name, fn->name);
                LLVMTypeRef ret_t = gen_llvm_type(&g, fn->ret);
                int has_self = (fn->nparams > 0 && strcmp(fn->params[0].name, "self") == 0);
                int np = fn->nparams + (has_self ? 0 : 1);
                LLVMTypeRef* pts = calloc(np > 0 ? np : 1, sizeof(LLVMTypeRef));
                int pi = 0;
                if (!has_self) {
                    pts[pi++] = LLVMPointerTypeInContext(ctx, 0);
                }
                for (int p = 0; p < fn->nparams; p++) {
                    pts[pi++] = gen_llvm_type(&g, fn->params[p].type);
                }
                LLVMTypeRef fn_t = LLVMFunctionType(ret_t, pts, np, 0);
                LLVMAddFunction(module, mname, fn_t);
                free(pts);
            }
        }
    }

    /* Pass 3: Emit function bodies */
    for (int i = 0; i < prog->nitems; i++) {
        Item* it = prog->items[i];
        if (it->kind == TOP_FN && it->fn) {
            gen_function_body(&g, it->fn, NULL);
        } else if (it->kind == TOP_IMPL && it->im) {
            ImplDef* im = it->im;
            for (int m = 0; m < im->nmethods; m++) {
                char mname[256];
                snprintf(mname, sizeof(mname), "%s_%s", im->target->name, im->methods[m]->name);
                gen_function_body(&g, im->methods[m], mname);
            }
        }
    }

    char* verify_err = NULL;
    if (LLVMVerifyModule(module, LLVMReturnStatusAction, &verify_err) != 0) {
        fprintf(stderr, "rokade [llvm warning]: %s\n", verify_err);
        LLVMDisposeMessage(verify_err);
    }

    LLVMDisposeBuilder(builder);
    return module;
}

static char* llvm_backend_emit_program(Sema* sema, Program* prog, int* out_len, int bounds_check) {
    (void)bounds_check;

    LLVMContextRef ctx = LLVMContextCreate();
    LLVMModuleRef module = llvm_backend_build_module(ctx, sema, prog);

    char* ir_str = LLVMPrintModuleToString(module);
    size_t len = strlen(ir_str);
    char* result = malloc(len + 1);
    if (result) {
        memcpy(result, ir_str, len + 1);
    }
    if (out_len) *out_len = (int)len;

    LLVMDisposeMessage(ir_str);
    LLVMDisposeModule(module);
    LLVMContextDispose(ctx);

    return result;
}

static int emit_module_to_obj(LLVMModuleRef module, const char* obj_path, int opt_level) {
    LLVMInitializeAllTargetInfos();
    LLVMInitializeAllTargets();
    LLVMInitializeAllTargetMCs();
    LLVMInitializeAllAsmPrinters();
    LLVMInitializeAllAsmParsers();

    char* triple = LLVMGetDefaultTargetTriple();
    LLVMTargetRef target = NULL;
    char* err = NULL;
    if (LLVMGetTargetFromTriple(triple, &target, &err) != 0) {
        fprintf(stderr, "rokade [llvm error]: cannot get target for triple '%s': %s\n", triple, err ? err : "unknown");
        if (err) LLVMDisposeMessage(err);
        LLVMDisposeMessage(triple);
        return 1;
    }

    LLVMCodeGenOptLevel opt = LLVMCodeGenLevelDefault;
    if (opt_level == 0) opt = LLVMCodeGenLevelNone;
    else if (opt_level == 1) opt = LLVMCodeGenLevelLess;
    else if (opt_level == 2) opt = LLVMCodeGenLevelDefault;
    else if (opt_level >= 3) opt = LLVMCodeGenLevelAggressive;

    LLVMTargetMachineRef tm = LLVMCreateTargetMachine(
        target, triple, "generic", "",
        opt, LLVMRelocPIC, LLVMCodeModelDefault);
    if (!tm) {
        fprintf(stderr, "rokade [llvm error]: failed to create TargetMachine\n");
        LLVMDisposeMessage(triple);
        return 1;
    }

    LLVMTargetDataRef td = LLVMCreateTargetDataLayout(tm);
    char* td_str = LLVMCopyStringRepOfTargetData(td);
    LLVMSetDataLayout(module, td_str);
    LLVMSetTarget(module, triple);
    LLVMDisposeMessage(td_str);
    LLVMDisposeTargetData(td);

    char* emit_err = NULL;
    if (LLVMTargetMachineEmitToFile(tm, module, obj_path, LLVMObjectFile, &emit_err) != 0) {
        fprintf(stderr, "rokade [llvm error]: failed to emit object file '%s': %s\n", obj_path, emit_err ? emit_err : "unknown");
        if (emit_err) LLVMDisposeMessage(emit_err);
        LLVMDisposeTargetMachine(tm);
        LLVMDisposeMessage(triple);
        return 1;
    }

    LLVMDisposeTargetMachine(tm);
    LLVMDisposeMessage(triple);
    return 0;
}

int llvm_backend_emit_obj(Sema* sema, Program* prog, const char* obj_path, int opt_level) {
    LLVMContextRef ctx = LLVMContextCreate();
    LLVMModuleRef module = llvm_backend_build_module(ctx, sema, prog);
    if (!module) {
        LLVMContextDispose(ctx);
        return 1;
    }
    int rc = emit_module_to_obj(module, obj_path, opt_level);
    LLVMDisposeModule(module);
    LLVMContextDispose(ctx);
    return rc;
}

int llvm_backend_compile_ll_to_obj(const char* ll_path, const char* obj_path, int opt_level) {
    LLVMContextRef ctx = LLVMContextCreate();
    LLVMMemoryBufferRef mem_buf = NULL;
    char* msg = NULL;
    if (LLVMCreateMemoryBufferWithContentsOfFile(ll_path, &mem_buf, &msg) != 0) {
        fprintf(stderr, "rokade [llvm error]: cannot read '%s': %s\n", ll_path, msg ? msg : "unknown");
        if (msg) LLVMDisposeMessage(msg);
        LLVMContextDispose(ctx);
        return 1;
    }

    LLVMModuleRef module = NULL;
    char* parse_err = NULL;
    if (LLVMParseIRInContext2(ctx, mem_buf, &module, &parse_err) != 0) {
        fprintf(stderr, "rokade [llvm error]: failed to parse LLVM IR in '%s': %s\n", ll_path, parse_err ? parse_err : "unknown");
        if (parse_err) LLVMDisposeMessage(parse_err);
        LLVMDisposeMemoryBuffer(mem_buf);
        LLVMContextDispose(ctx);
        return 1;
    }
    LLVMDisposeMemoryBuffer(mem_buf);

    int rc = emit_module_to_obj(module, obj_path, opt_level);
    LLVMDisposeModule(module);
    LLVMContextDispose(ctx);
    return rc;
}

int llvm_backend_jit_run(Sema* sema, Program* prog, int argc, char** argv) {
    LLVMLinkInMCJIT();
    LLVMInitializeAllTargetInfos();
    LLVMInitializeAllTargets();
    LLVMInitializeAllTargetMCs();
    LLVMInitializeAllAsmPrinters();
    LLVMInitializeAllAsmParsers();

    LLVMLoadLibraryPermanently(NULL);

    LLVMContextRef ctx = LLVMContextCreate();
    LLVMModuleRef module = llvm_backend_build_module(ctx, sema, prog);
    if (!module) {
        LLVMContextDispose(ctx);
        return -1;
    }

    LLVMExecutionEngineRef ee = NULL;
    char* ee_err = NULL;
    if (LLVMCreateExecutionEngineForModule(&ee, module, &ee_err) != 0) {
        fprintf(stderr, "rokade [llvm jit error]: failed to create execution engine: %s\n", ee_err ? ee_err : "unknown");
        if (ee_err) LLVMDisposeMessage(ee_err);
        LLVMDisposeModule(module);
        LLVMContextDispose(ctx);
        return -1;
    }

    uint64_t main_addr = LLVMGetFunctionAddress(ee, "main");
    if (!main_addr) {
        fprintf(stderr, "rokade [llvm jit error]: 'main' function not found\n");
        LLVMDisposeExecutionEngine(ee);
        LLVMContextDispose(ctx);
        return -1;
    }

    int (*main_fn)(int, char**) = (int (*)(int, char**))(uintptr_t)main_addr;
    int exit_code = main_fn(argc, argv);

    LLVMDisposeExecutionEngine(ee);
    LLVMContextDispose(ctx);
    return exit_code;
}

Backend* llvm_backend_create(void) {
    Backend* b = calloc(1, sizeof(Backend));
    if (!b) return NULL;
    b->name = "llvm";
    b->emit_program = llvm_backend_emit_program;
    b->emit_obj = llvm_backend_emit_obj;
    b->jit_run = llvm_backend_jit_run;
    b->destroy = llvm_backend_destroy;
    return b;
}

#else

Backend* llvm_backend_create(void) {
    fprintf(stderr, "rokade: LLVM backend is not enabled in this build.\n");
    return NULL;
}

int llvm_backend_emit_obj(Sema* sema, Program* prog, const char* obj_path, int opt_level) {
    (void)sema; (void)prog; (void)obj_path; (void)opt_level;
    fprintf(stderr, "rokade: LLVM backend is not enabled in this build.\n");
    return 1;
}

int llvm_backend_jit_run(Sema* sema, Program* prog, int argc, char** argv) {
    (void)sema; (void)prog; (void)argc; (void)argv;
    fprintf(stderr, "rokade: LLVM backend is not enabled in this build.\n");
    return -1;
}

int llvm_backend_compile_ll_to_obj(const char* ll_path, const char* obj_path, int opt_level) {
    (void)ll_path; (void)obj_path; (void)opt_level;
    fprintf(stderr, "rokade: LLVM backend is not enabled in this build.\n");
    return 1;
}

#endif
