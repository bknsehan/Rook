#ifndef RK_AST_H
#define RK_AST_H

typedef struct AstType AstType;
typedef struct Expr Expr;
typedef struct Stmt Stmt;
typedef struct Decl Decl;
typedef struct FnDef FnDef;

/* goto-definition targets attached to E_IDENT usages by the type checker.
   `def` points at an AST-owned node (Decl/FnDef/StructDef/EnumDef/EnumVariant),
   which outlives the checker's ephemeral scopes, so it is safe to read after
   sema_check returns. */
typedef enum {
    DEF_NONE = 0,
    DEF_FN,       /* def = FnDef* */
    DEF_STRUCT,   /* def = StructDef* */
    DEF_ENUM,     /* def = EnumDef* */
    DEF_VARIANT,  /* def = EnumVariant* */
    DEF_VAR       /* def = Decl* */
} DefKind;

typedef enum {
    E_LITERAL,      /* str = raw lexeme of number/string/char literal */
    E_IDENT,        /* str = name */
    E_CALL,         /* a = callee, items = args */
    E_MEMBER,       /* a = obj, str = member name */
    E_ARROW,        /* a = obj, str = member name */
    E_INDEX,        /* a = obj, b = index */
    E_UNARY,        /* str = op (- ! * & ++ --), a = operand */
    E_POST,         /* str = op (++ --), a = operand */
    E_BINARY,       /* str = op, a = lhs, b = rhs */
    E_TERNARY,      /* a = cond, b = then, c = else */
    E_ASSIGN,       /* str = op (= += -= *= /= %=), a = lhs, b = rhs */
    E_CAST,         /* type, a = operand */
    E_COMPOUND,     /* type, citems: (T){ 1, .f = 2 } */
    E_NAMED_INIT,   /* type, nfields: T{ a: 1, b: 2 } */
    E_BRACE_INIT,   /* items: { 1, 2 } */
    E_PAREN,        /* a = inner */
    E_SIZEOF_T,     /* type */
    E_SIZEOF_E,     /* a = expr */
    E_ARR_LIT,      /* items: [ a, b ] */
    E_RANGE,        /* a = lo, b = hi: lo..=hi */
    E_QUESTION,     /* a = inner expr: error-propagation `expr?` */
    E_MATCH,          /* a = scrutinee, marms/nmarms = arms (match expr) */
} ExprKind;

typedef struct AstType {
    char* qual;         /* "const " / "unsigned " / "" */
    char* name;
    int ptrs;           /* trailing * count (also absorbs leading * style) */
} AstType;

typedef struct NamedInitField {
    char* name;
    Expr* e;
} NamedInitField;

typedef struct CompItem {
    char* name;         /* NULL for positional item */
    Expr* e;
} CompItem;

typedef struct MatchArm MatchArm;  /* forward (defined below Expr) */

typedef struct Expr {
    ExprKind kind;
    char* str;
    AstType* type;
    Expr* a;
    Expr* b;
    Expr* c;
    Expr** items;
    int nitems;
    NamedInitField* nfields;
    int nnfields;
    CompItem* citems;   /* E_COMPOUND: mixed positional/designated */
    int ncitems;
    MatchArm* marms;   /* E_MATCH */
    int nmarms;
    int start;          /* byte offset of this node in source (for diagnostics) */
    int len;            /* span in bytes */
    int line;
    int col;
    DefKind def_kind;   /* goto-definition target for E_IDENT usages */
    void* def;          /* AST-owned node (see DefKind) */
} Expr;

typedef enum {
    S_BLOCK, S_EXPR, S_DECL, S_IF, S_WHILE, S_FOR, S_FORIN,
    S_SWITCH, S_MATCH, S_RETURN, S_BREAK, S_CONTINUE, S_EMPTY,
    S_DEFER,
} StmtKind;

typedef enum { DECL_LET, DECL_TYPED, DECL_C } DeclStyle;

typedef struct Decl {
    DeclStyle style;
    char* name;
    AstType* type;      /* NULL for `let x` without annotation */
    Expr* dim;          /* DECL_C array dimension */
    Expr* init;
    int start;
    int len;
    int line;
    int col;
} Decl;

typedef struct SwitchArm {
    Expr** labels;
    int nlabels;
    int is_default;     /* default: / else -> */
    int arrow;          /* -> style */
    Stmt* body;         /* colon: S_BLOCK of stmts; arrow: single stmt */
} SwitchArm;

typedef struct MatchArm {
    Expr* pattern;      /* literal / range / _ ident */
    Expr* body;
} MatchArm;

typedef struct Stmt {
    StmtKind kind;
    Expr* e;                /* S_EXPR */
    Decl* decl;             /* S_DECL */
    Expr* cond;             /* S_IF / S_WHILE */
    Stmt* then;             /* S_IF */
    Stmt* els;              /* S_IF (may be another S_IF) */
    Stmt* body;             /* S_WHILE / S_FOR / S_FORIN */
    Decl* init_decl;        /* S_FOR (C-style init) */
    Expr* init_expr;        /* S_FOR (expression init) */
    Expr* step;             /* S_FOR */
    char* var;              /* S_FORIN */
    Expr* iter;             /* S_FORIN */
    SwitchArm* arms;        /* S_SWITCH */
    int narms;
    MatchArm* marms;        /* S_MATCH */
    int nmarms;
    Stmt** stmts;           /* S_BLOCK */
    int nstmts;
    Stmt* defer;            /* S_DEFER (the deferred statement/block) */
    int start;
    int len;
    int line;
    int col;
} Stmt;

typedef enum { TOP_RAW, TOP_FN, TOP_STRUCT, TOP_IMPL, TOP_ENUM, TOP_MODULE, TOP_IMPORT } TopKind;

typedef struct Param {
    char* name;
    AstType* type;
} Param;

typedef struct FnDef {
    char* name;
    Param* params;
    int nparams;
    AstType* ret;           /* NULL when absent */
    Stmt* body;             /* S_BLOCK */
    int is_extern;          /* 1 for `extern fn` (declared in C, no body) */
    int line;
    int col;
} FnDef;

typedef enum { FIELD_YUP, FIELD_C } FieldStyle;

typedef struct StructField {
    FieldStyle style;
    char* name;
    AstType* type;
    Expr* dim;              /* FIELD_C array dimension */
} StructField;

typedef struct StructDef {
    char* name;
    char* parent;
    StructField* fields;
    int nfields;
    int is_object;       /* 1: `object` (OOP: inheritance + impl); 0: plain C `struct` */
    int line;
    int col;
} StructDef;

typedef struct EnumVariant {
    char* name;           /* "Up" */
    StructField* fields;  /* payload named fields; NULL/0 == unit variant */
    int nfields;
    int line;
    int col;
} EnumVariant;

typedef struct EnumDef {
    char* name;
    EnumVariant* variants;
    int nvariants;
    int is_c_enum;       /* 1: plain C-style `enum Name { A, B }` (verbatim, no payload) */
    int line;
    int col;
} EnumDef;

typedef struct ImplDef {
    AstType* target;
    FnDef** methods;
    int nmethods;
    int start;              /* byte offset of the `impl` keyword (diagnostics) */
    int line;
    int col;
} ImplDef;

typedef struct Item {
    TopKind kind;
    char* raw;              /* TOP_RAW: source slice */
    int raw_len;
    FnDef* fn;
    StructDef* st;
    ImplDef* im;
    EnumDef* ed;        /* TOP_ENUM */
    char* modname;         /* TOP_MODULE */
    char* impname;         /* TOP_IMPORT */
} Item;

typedef struct Program {
    Item** items;
    int nitems;
} Program;

AstType* ast_type_new(void);
void ast_type_free(AstType* t);
void freed_init(void);
Expr* ast_expr_new(ExprKind k);
Stmt* ast_stmt_new(StmtKind k);
Decl* ast_decl_new(DeclStyle s, const char* name, AstType* type, Expr* init);
Item* ast_item_new(TopKind k);
void ast_program_add(Program* p, Item* it);

int type_eq(AstType* a, AstType* b);
int expr_eq(Expr* a, Expr* b);
int stmt_eq(Stmt* a, Stmt* b);
int item_eq(Item* a, Item* b);
int program_eq(Program* a, Program* b);

void ast_dump(Program* p);
void program_free(Program* p);

#endif
