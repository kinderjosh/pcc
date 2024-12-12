#ifndef AST_H
#define AST_H

#include "token.h"
#include <stdio.h>
#include <stdbool.h>

typedef enum {
    AST_ROOT,
    AST_INT,
    AST_FLOAT,
    AST_VAR,
    AST_FUNC,
    AST_CALL,
    AST_ASSIGN,
    AST_RET,
    AST_MATH,
    AST_OPER,
    AST_MATH_VAR,
    AST_IF_ELSE
} ASTType;

typedef struct AST AST;

typedef struct AST {
    ASTType type;
    char *scope_def;
    char *func_def;
    size_t ln;
    size_t col;
    bool active;

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

        struct {
            AST **expr;
            size_t expr_cnt;
        } math;

        struct {
            TokType kind;
        } oper;

        struct {
            char *stack_rbp;
            char *reg_name;
            bool is_float;
        } math_var;

        struct {
            AST **exprs;
            AST **body;
            AST **else_body;
            size_t exprs_cnt;
            size_t body_cnt;
            size_t else_body_cnt;
        } if_else;
    };
} AST;

AST *ast_init(ASTType type, char *scope_def, char *func_def, size_t ln, size_t col);
void ast_fields_del(AST *ast);
void ast_del(AST *ast);

#endif