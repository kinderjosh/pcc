#include "parser.h"
#include "token.h"
#include "lexer.h"
#include "ast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <limits.h>
#include <errno.h>

extern const char *tok_types[];
extern const char *ast_types[];

AST **sym_tab;
size_t sym_cnt = 0;

AST *sym_find(ASTType type, char *scope, char *name) {
    AST *sym;
    for (size_t i = 0; i < sym_cnt; i++) {
        sym = sym_tab[i];

        if (sym->type != type || (strcmp(sym->scope_def, scope) != 0 && strcmp(sym->scope_def, "<global>") != 0 && strcmp(scope, "<global>") != 0))
            continue;
        else if ((type == AST_FUNC && strcmp(sym->func.name, name) == 0) || (type == AST_ASSIGN && strcmp(sym->assign.name, name) == 0))
            return sym;
    }

    return NULL;
}

void sym_append(AST *sym) {
    sym_tab = realloc(sym_tab, (sym_cnt + 1) * sizeof(AST *));
    sym_tab[sym_cnt++] = sym;
}

bool is_type(char *id) {
    if (strcmp(id, "void") == 0 || strcmp(id, "char") == 0 || strcmp(id, "int") == 0 || strcmp(id, "float") == 0)
        return true;

    return false;
}

Prs *prs_init(char *file) {
    Prs *prs = malloc(sizeof(Prs));
    prs->file = file;
    prs->lex = lex_init(file);
    prs->tok = lex_next(prs->lex);
    prs->cur_scope = strdup("<global>");
    prs->cur_func = strdup("<global>");
    return prs;
}

void prs_del(Prs *prs) {
    tok_del(prs->tok);
    lex_del(prs->lex);
    free(prs->cur_scope);
    free(prs->cur_func);
    free(prs);
}

TokType prs_eat(Prs *prs, TokType type) {
    if (type != prs->tok->type) {
        fprintf(stderr, "%s:%zu:%zu: Error: Expected '%s' but found '%s'.\n", prs->file, prs->tok->ln, prs->tok->col, tok_types[type], tok_types[prs->tok->type]);
        exit(EXIT_FAILURE);
    }

    TokType eaten = prs->tok->type;
    if (eaten != TOK_EOF) {
        tok_del(prs->tok);
        prs->tok = lex_next(prs->lex);
    }
    return eaten;
}

AST **prs_body(Prs *prs, size_t *cnt, bool allow_no_braces) {
    AST **body = calloc(1, sizeof(AST *));
    size_t body_cnt = 0;
    AST *stmt;

    if (!allow_no_braces)
        prs_eat(prs, TOK_LBRACE);

    while (prs->tok->type != TOK_RBRACE && prs->tok->type != TOK_EOF) {
        stmt = prs_stmt(prs);
        switch (stmt->type) {
            case AST_CALL:
            case AST_ASSIGN:
            case AST_RET:
                prs_eat(prs, TOK_SEMI);
                break;
            default:
                fprintf(stderr, "%s:%zu:%zu: Error: Invalid statement '%s' in function '%s'.\n", prs->file, stmt->ln, stmt->col, ast_types[stmt->type], prs->cur_func);
                exit(EXIT_FAILURE);
        }

        body = realloc(body, (body_cnt + 1) * sizeof(AST *));
        body[body_cnt++] = stmt;

        if (allow_no_braces)
            break;
    }

    if (!allow_no_braces)
        prs_eat(prs, TOK_RBRACE);

    *cnt = body_cnt;
    return body;
}

AST *prs_value(Prs *prs, char *type) {
    AST *value = prs_stmt(prs);

    if (type == NULL)
        return value;

    switch (value->type) {
        case AST_INT:
        case AST_FLOAT:
            if (value->type == AST_FLOAT && strcmp(type, "float") != 0)
                value->type = AST_INT;

            if (strcmp(type, "char") == 0 && (value->data.digit > CHAR_MAX || value->data.digit < CHAR_MIN))
                value->data.digit = (unsigned char)((signed char)value->data.digit);
            else if (strcmp(type, "char") != 0 && (value->data.digit > INT_MAX || value->data.digit < INT_MIN))
                value->data.digit = (unsigned int)((signed int)value->data.digit);
            break;
        case AST_VAR: break;
        case AST_CALL: {
            AST *sym = sym_find(AST_FUNC, "<global>", value->call.name);
            if (strcmp(sym->func.type, "void") == 0) {
                fprintf(stderr, "%s:%zu:%zu: Error: Function '%s' doesn't return a value.\n", prs->file, value->ln, value->col, value->call.name);
                exit(EXIT_FAILURE);
            }
            break;
        }
        default:
            fprintf(stderr, "%s:%zu:%zu: Error: Invalid value '%s'.\n", prs->file, value->ln, value->col, ast_types[value->type]);
            exit(EXIT_FAILURE);
    }

    return value;
}

AST *prs_id_func(Prs *prs, char *name, char *type, size_t ln, size_t col) {
    AST *sym = sym_find(AST_FUNC, "<global>", name);
    if (sym != NULL) {
        fprintf(stderr, "%s:%zu:%zu: Error: Redefinition of function '%s'; first defined at %zu:%zu.\n", prs->file, ln, col, name, sym->ln, sym->col);
        exit(EXIT_FAILURE);
    }

    prs->cur_scope = realloc(prs->cur_scope, (strlen(name) + 1) * sizeof(char));
    strcpy(prs->cur_scope, name);
    prs->cur_func = realloc(prs->cur_func, (strlen(name) + 1) * sizeof(char));
    strcpy(prs->cur_func, name);

    AST **params = calloc(1, sizeof(AST *));
    size_t params_cnt = 0;
    AST *param;
    prs_eat(prs, TOK_LPAREN);

    while (prs->tok->type != TOK_RPAREN && prs->tok->type != TOK_EOF) {
        if (params_cnt == 6) {
            fprintf(stderr, "%s:%zu:%zu: Error: Maximum parameter count of 6 exceeded.\n", prs->file, prs->tok->ln, prs->tok->col);
            exit(EXIT_FAILURE);
        } else if (params_cnt > 0)
            prs_eat(prs, TOK_COMMA);

        param = prs_stmt(prs);
        if (param->type != AST_ASSIGN) {
            fprintf(stderr, "%s:%zu:%zu: Error: Expected parameter definition but found '%s'.\n", prs->file, param->ln, param->col, ast_types[param->type]);
            exit(EXIT_FAILURE);
        } else if (param->assign.value != NULL) {
            fprintf(stderr, "%s:%zu:%zu: Error: Assigning parameter '%s' outside of function body.\n", prs->file, param->ln, param->col, param->assign.name);
            exit(EXIT_FAILURE);
        }

        params = realloc(params, (params_cnt + 1) * sizeof(AST *));
        params[params_cnt++] = param;
    }

    prs_eat(prs, TOK_RPAREN);

    AST *ast = ast_init(AST_FUNC, "<global>", "<global>", ln, col);
    ast->func.name = name;
    ast->func.type = type;
    ast->func.params = params;
    ast->func.params_cnt = params_cnt;
    sym_append(ast);

    size_t body_cnt;
    AST **body = prs_body(prs, &body_cnt, false);
    AST *ret = NULL;

    if (body_cnt > 0 && body[body_cnt - 1]->type == AST_RET)
        ret = body[body_cnt - 1];

    if (strcmp(type, "void") != 0 && ret == NULL) {
        fprintf(stderr, "%s:%zu:%zu: Error: Missing return statement in function '%s' of type '%s'.\n", prs->file, ln, col, name, type);
        exit(EXIT_FAILURE);
    }

    ast->func.body = body;
    ast->func.body_cnt = body_cnt;
    ast->func.ret = ret;

    prs->cur_scope = realloc(prs->cur_scope, 9 * sizeof(char));
    strcpy(prs->cur_scope, "<global>");
    prs->cur_func = realloc(prs->cur_func, 9 * sizeof(char));
    strcpy(prs->cur_func, "<global>");

    return ast;
}

AST *prs_id_assign(Prs *prs, char *name, char *type, size_t ln, size_t col) {
    AST *sym = sym_find(AST_ASSIGN, prs->cur_scope, name);
    if (type != NULL && sym != NULL) {
        fprintf(stderr, "%s:%zu:%zu: Error: Redefinition of variable '%s'; first defined at %zu:%zu.\n", prs->file, ln, col, name, sym->ln, sym->col);
        exit(EXIT_FAILURE);
    }

    AST *ast = ast_init(AST_ASSIGN, prs->cur_scope, prs->cur_func, ln, col);
    ast->assign.name = name;
    ast->assign.type = type;
    ast->assign.rbp = NULL;

    if (prs->tok->type == TOK_EQUAL) {
        prs_eat(prs, TOK_EQUAL);
        ast->assign.value = prs_value(prs, type == NULL ? sym->assign.type : type);
    } else
        ast->assign.value = NULL;

    if (type != NULL)
        sym_append(ast);

    return ast;
}

AST *prs_id_ret(Prs *prs, size_t ln, size_t col) {
    AST *sym = sym_find(AST_FUNC, "<global>", prs->cur_func);
    if (sym == NULL) {
        fprintf(stderr, "%s:%zu:%zu: Error: Invalid statement 'Return' outside of a function.\n", prs->file, ln, col);
        exit(EXIT_FAILURE);
    }

    if (strcmp(sym->func.type, "void") == 0 && prs->tok->type != TOK_SEMI) {
        fprintf(stderr, "%s:%zu:%zu: Error: Unexpected return value in function '%s' of type 'void'.\n", prs->file, prs->tok->ln, prs->tok->col, prs->cur_func);
        exit(EXIT_FAILURE);
    } else if (strcmp(sym->func.type, "void") != 0 && prs->tok->type == TOK_SEMI) {
        fprintf(stderr, "%s:%zu:%zu: Error: Missing return value in function '%s' of type '%s'.\n", prs->file, prs->tok->ln, prs->tok->col, prs->cur_func, sym->func.type);
        exit(EXIT_FAILURE);
    }

    AST *ast = ast_init(AST_RET, prs->cur_scope, prs->cur_func, ln, col);
    ast->ret.value = prs->tok->type == TOK_SEMI ? NULL : prs_value(prs, sym->func.type);
    return ast;
}

AST *prs_id_call(Prs *prs, char *name, size_t ln, size_t col) {
    AST *sym = sym_find(AST_FUNC, "<global>", name);
    if (sym == NULL) {
        fprintf(stderr, "%s:%zu:%zu: Error: Undefined function '%s'.\n", prs->file, ln, col, name);
        exit(EXIT_FAILURE);
    }

    AST **args = calloc(1, sizeof(AST *));
    size_t args_cnt = 0;
    AST *param;
    prs_eat(prs, TOK_LPAREN);

    while (prs->tok->type != TOK_RPAREN && prs->tok->type != TOK_EOF) {
        if (args_cnt == 6) {
            fprintf(stderr, "%s:%zu:%zu: Error: Maximum argument count of 6 exceeded.\n", prs->file, prs->tok->ln, prs->tok->col);
            exit(EXIT_FAILURE);
        } else if (args_cnt == sym->func.params_cnt) {
            fprintf(stderr, "%s:%zu:%zu: Error: Excessive arguments in call to function '%s'; expected %zu but found %zu.\n", prs->file, ln, col, name, sym->func.params_cnt, args_cnt);
            exit(EXIT_FAILURE);
        } else if (args_cnt > 0)
            prs_eat(prs, TOK_COMMA);

        param = sym->func.params[args_cnt];
        args = realloc(args, (args_cnt + 1) * sizeof(AST *));
        args[args_cnt++] = prs_value(prs, param->assign.type);
    }

    prs_eat(prs, TOK_RPAREN);

    if (args_cnt < sym->func.params_cnt) {
        fprintf(stderr, "%s:%zu:%zu: Error: Missing arguments in call to function '%s'; expected %zu but found %zu.\n", prs->file, ln, col, name, sym->func.params_cnt, args_cnt);
        exit(EXIT_FAILURE);
    }

    AST *ast = ast_init(AST_CALL, prs->cur_scope, prs->cur_func, ln, col);
    ast->call.name = name;
    ast->call.args = args;
    ast->call.args_cnt = args_cnt;
    return ast;
}

AST *prs_id(Prs *prs) {
    size_t ln = prs->tok->ln;
    size_t col = prs->tok->col;

    char *id = strdup(prs->tok->value);
    prs_eat(prs, TOK_ID);

    if (is_type(id)) {
        char *name = strdup(prs->tok->value);
        prs_eat(prs, TOK_ID);

        if (prs->tok->type == TOK_LPAREN)
            return prs_id_func(prs, name, id, ln, col);

        return prs_id_assign(prs, name, id, ln, col);
    } else if (strcmp(id, "return") == 0) {
        free(id);
        return prs_id_ret(prs, ln, col);
    } else if (prs->tok->type == TOK_EQUAL)
        return prs_id_assign(prs, id, NULL, ln, col);
    else if (prs->tok->type == TOK_LPAREN)
        return prs_id_call(prs, id, ln, col);
    else if (sym_find(AST_ASSIGN, prs->cur_scope, id) != NULL) {
        AST *ast = ast_init(AST_VAR, prs->cur_scope, prs->cur_func, ln, col);
        ast->var.name = id;
        return ast;
    }

    fprintf(stderr, "%s:%zu:%zu: Error: Unknown identifier '%s'.\n", prs->file, ln, col, id);
    exit(EXIT_FAILURE);
}

AST *prs_data(Prs *prs) {
    AST *ast = ast_init(prs->tok->type == TOK_INT ? AST_INT : AST_FLOAT, prs->cur_scope, prs->cur_func, prs->tok->ln, prs->tok->col);
    char *endptr;
    ast->data.digit = strtold(prs->tok->value, &endptr);

    if (endptr == prs->tok->value || errno == ERANGE) {
        fprintf(stderr, "%s:%zu:%zu: Error: Digit conversion failed: %s.\n", prs->file, prs->tok->ln, prs->tok->col, strerror(errno));
        exit(EXIT_FAILURE);
    }

    prs_eat(prs, prs->tok->type);
    return ast;
}

AST *prs_stmt(Prs *prs) {
    switch (prs->tok->type) {
        case TOK_ID: return prs_id(prs);
        case TOK_INT:
        case TOK_FLOAT: return prs_data(prs);
        default:
            fprintf(stderr, "%s:%zu:%zu: Error: Invalid statement '%s'.\n", prs->file, prs->tok->ln, prs->tok->col, tok_types[prs->tok->type]);
            exit(EXIT_FAILURE);
    }
}

AST *prs_file(char *file) {
    Prs *prs = prs_init(file);
    AST **asts = calloc(1, sizeof(AST *));
    size_t asts_cnt = 0;
    AST *stmt;

    sym_tab = calloc(1, sizeof(AST *));

    while (prs->tok->type != TOK_EOF) {
        stmt = prs_stmt(prs);
        if (stmt->type != AST_FUNC && stmt->type != AST_ASSIGN) {
            fprintf(stderr, "%s:%zu:%zu: Error: Invalid statement '%s' outside of a function.\n", prs->file, stmt->ln, stmt->col, ast_types[stmt->type]);
            exit(EXIT_FAILURE);
        } else if (stmt->type == AST_ASSIGN) {
            if (stmt->assign.type == NULL) {
                fprintf(stderr, "%s:%zu:%zu: Error: Assigning variable '%s' outside of a function.\n", prs->file, stmt->ln, stmt->col, stmt->assign.name);
                exit(EXIT_FAILURE);
            }

            prs_eat(prs, TOK_SEMI);
        }

        asts = realloc(asts, (asts_cnt + 1) * sizeof(AST *));
        asts[asts_cnt++] = stmt;
    }

    if (sym_find(AST_FUNC, "<global>", "main") == NULL) {
        fprintf(stderr, "%s: Error: Missing entrypoint 'main'.\n", prs->file);
        exit(EXIT_FAILURE);
    }

    prs_del(prs);

    AST *root = ast_init(AST_ROOT, "<internal>", "<internal>", 0, 0);
    root->root.asts = asts;
    root->root.asts_cnt = asts_cnt;
    return root;
}