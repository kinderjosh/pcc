#include "ast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

const char *ast_types[] = {
    [AST_ROOT] = "root",
    [AST_INT] = "int",
    [AST_FLOAT] = "float",
    [AST_VAR] = "variable",
    [AST_FUNC] = "function",
    [AST_CALL] = "function call",
    [AST_ASSIGN] = "assignment",
    [AST_RET] = "return",
    [AST_MATH] = "math expression",
    [AST_OPER] = "operator",
    [AST_MATH_VAR] = "math var"
};

AST *ast_init(ASTType type, char *scope_def, char *func_def, size_t ln, size_t col) {
    AST *ast = malloc(sizeof(AST));
    ast->type = type;
    ast->scope_def = strdup(scope_def);
    ast->func_def = strdup(func_def);
    ast->ln = ln;
    ast->col = col;
    ast->active = true;
    return ast;
}

void ast_fields_del(AST *ast) {
    switch (ast->type) {
        case AST_ROOT:
            for (size_t i = 0; i < ast->root.asts_cnt; i++)
                ast_del(ast->root.asts[i]);
            free(ast->root.asts);
            break;
        case AST_VAR:
            free(ast->var.name);
            break;
        case AST_FUNC:
            free(ast->func.name);
            free(ast->func.type);

            for (size_t i = 0; i < ast->func.params_cnt; i++)
                ast_del(ast->func.params[i]);
            free(ast->func.params);

            for (size_t i = 0; i < ast->func.body_cnt; i++)
                ast_del(ast->func.body[i]);
            free(ast->func.body);
            break;
        case AST_CALL:
            free(ast->call.name);

            for (size_t i = 0; i < ast->call.args_cnt; i++)
                ast_del(ast->call.args[i]);
            free(ast->call.args);
            break;
        case AST_ASSIGN:
            free(ast->assign.name);

            if (ast->assign.type != NULL)
                free(ast->assign.type);

            if (ast->assign.rbp != NULL)
                free(ast->assign.rbp);

            if (ast->assign.value != NULL)
                ast_del(ast->assign.value);
            break;
        case AST_RET:
            if (ast->ret.value != NULL)
                ast_del(ast->ret.value);
            break;
        case AST_MATH:
            for (size_t i = 0; i < ast->math.expr_cnt; i++)
                ast_del(ast->math.expr[i]);

            free(ast->math.expr);
            break;
        case AST_MATH_VAR:
            if (ast->math_var.stack_rbp != NULL)
                free(ast->math_var.stack_rbp);
            break;
        default: break;
    }
}

void ast_del(AST *ast) {
    ast_fields_del(ast);
    free(ast->scope_def);
    free(ast->func_def);
    free(ast);
}