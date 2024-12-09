#ifndef AST_H
#define AST_H

#include <stdio.h>

typedef enum {
    AST_ROOT,
    AST_INT,
    AST_FLOAT,
    AST_VAR,
    AST_FUNC,
    AST_CALL,
    AST_ASSIGN,
    AST_RET
} ASTType;

typedef struct AST AST;

typedef struct AST {
    ASTType type;
    char *scope_def;
    char *func_def;
    size_t ln;
    size_t col;

    union {
        struct {
            AST **asts;
            size_t asts_cnt;
        } root;

        struct {
            long double digit;
        } data;

        struct {
            char *name;
        } var;

        struct {
            char *name;
            char *type;
            AST **params;
            AST **body;
            AST *ret;
            size_t params_cnt;
            size_t body_cnt;
        } func;

        struct {
            char *name;
            AST **args;
            size_t args_cnt;
        } call;

        struct {
            char *name;
            char *type;
            char *rbp;
            AST *value;
        } assign;

        struct {
            AST *value;
        } ret;
    };
} AST;

AST *ast_init(ASTType type, char *scope_def, char *func_def, size_t ln, size_t col);
void ast_del(AST *ast);

#endif