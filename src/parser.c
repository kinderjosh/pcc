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
#include <assert.h>

extern const char *tok_types[];
extern const char *ast_types[];

AST **sym_tab;
size_t sym_cnt = 0;

/* Too lazy to explain this
 * Example:
 *
 * main
 * main-if:2:4
 * main-if:2:4-if:3:8
 * 
 * Check if scopes have common sections like above
 */
bool is_in_scope(char *haystack, char *needle) {
    if (strcmp(haystack, needle) == 0 || strcmp(haystack, "<global>") == 0 || strcmp(needle, "<global>") == 0)
        return true;

    char *haystack_cpy = strdup(haystack);
    char *needle_cpy = strdup(needle);

    char *haystack_rest = haystack_cpy;
    char *needle_rest = needle_cpy;

    char *haystack_tok;
    char *needle_tok;

    bool found = false;
    
    do {
        haystack_tok = strtok_r(haystack_rest, "-", &haystack_rest);
        if (haystack_tok == NULL) {
            found = true;
            break;
        }

        needle_tok = strtok_r(needle_rest, "-", &needle_rest);
        if (needle_tok == NULL)
            break;

        if (strcmp(haystack_tok, needle_tok) != 0)
            break;
    } while (haystack_tok != NULL);

    free(haystack_cpy);
    free(needle_cpy);
    return found;
}

AST *sym_find(ASTType type, char *scope, char *name) {
    AST *sym;
    for (size_t i = 0; i < sym_cnt; i++) {
        sym = sym_tab[i];

        if (sym->type != type || !is_in_scope(sym->scope_def, scope))
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

bool ast_is_float(AST *ast) {
    AST *sym;

    switch (ast->type) {
        case AST_FLOAT: return true;
        case AST_VAR:
            sym = sym_find(AST_ASSIGN, ast->scope_def, ast->var.name);
            if (strcmp(sym->assign.type, "float") == 0)
                return true;
            break;
        case AST_FUNC:
            sym = sym_find(AST_FUNC, "<global>", ast->func.name);
            if (strcmp(sym->func.type, "float") == 0)
                return true;
            break;
        case AST_CALL:
            sym = sym_find(AST_FUNC, "<global>", ast->call.name);
            if (strcmp(sym->func.type, "float") == 0)
                return true;
            break;
        default: break;
    }

    return false;
}

bool digit_is_float(long double digit) {
    char buf[80];
    sprintf(buf, "%Lf", digit);

    char *tok = strtok(buf, ".");
    if (tok == NULL)
        return false;

    tok = strtok(NULL, ".");
    assert(tok != NULL);

    char *endptr;
    long long int mantissa = strtoll(tok, &endptr, 10);

    if (endptr == tok || errno == ERANGE)
        return false;
    else if (mantissa > 0)
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
    prs->in_math = false;
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
        fprintf(stderr, "%s:%zu:%zu: error: expected '%s' but found '%s'\n", prs->file, prs->tok->ln, prs->tok->col, tok_types[type], tok_types[prs->tok->type]);
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
    bool ate_first = false;

    if (!allow_no_braces || prs->tok->type == TOK_LBRACE) {
        prs_eat(prs, TOK_LBRACE);
        ate_first = true;
        allow_no_braces = false;
    }

    while (prs->tok->type != TOK_RBRACE && prs->tok->type != TOK_EOF) {
        stmt = prs_stmt(prs);
        switch (stmt->type) {
            case AST_CALL:
                if (strcmp(prs->cur_scope, prs->cur_func) == 0 && strcmp(prs->cur_func, stmt->call.name) == 0) {
                    fprintf(stderr, "%s:%zu:%zu: error: call to function '%s' will result in infinite recursion\n", prs->file, stmt->ln, stmt->col, stmt->call.name);
                    exit(EXIT_FAILURE);
                }

                prs_eat(prs, TOK_SEMI);
                break;
            case AST_ASSIGN:
            case AST_RET:
                prs_eat(prs, TOK_SEMI);
                break;
            case AST_IF_ELSE:
            case AST_WHILE:
            case AST_FOR: break;
            case AST_SUBSCR:
                if (stmt->subscr.value == NULL)
                    goto prs_body_invalid_stmt;

                prs_eat(prs, TOK_SEMI);
                break;
            case AST_DEREF:
                if (stmt->deref.value == NULL)
                    goto prs_body_invalid_stmt;

                prs_eat(prs, TOK_SEMI);
                break;
            default:
prs_body_invalid_stmt:
                fprintf(stderr, "%s:%zu:%zu: error: invalid statement '%s' in function '%s'\n", prs->file, stmt->ln, stmt->col, ast_types[stmt->type], prs->cur_func);
                exit(EXIT_FAILURE);
        }

        body = realloc(body, (body_cnt + 1) * sizeof(AST *));
        body[body_cnt++] = stmt;

        if (allow_no_braces)
            break;
    }

    if (!allow_no_braces || (prs->tok->type == TOK_RBRACE && ate_first))
        prs_eat(prs, TOK_RBRACE);

    *cnt = body_cnt;
    return body;
}

AST *prs_value(Prs *prs, char *type);

AST *prs_math(Prs *prs, AST *first) {
    AST **expr = calloc(1, sizeof(AST *));
    size_t expr_cnt = 1;
    AST *oper;
    AST *value;
    bool is_float = ast_is_float(first);
    bool contains_mod = false;
    bool is_const = first->type == AST_INT || first->type == AST_FLOAT ? true : false;

    expr[0] = first;
    prs->in_math = true;

    while (prs->tok->type == TOK_PLUS || prs->tok->type == TOK_MINUS || prs->tok->type == TOK_STAR || prs->tok->type == TOK_SLASH || prs->tok->type == TOK_PERCENT) {
        oper = ast_init(AST_OPER, prs->cur_scope, prs->cur_func, prs->tok->ln, prs->tok->col);
        oper->oper.kind = prs_eat(prs, prs->tok->type);

        if (oper->oper.kind == TOK_PERCENT)
            contains_mod = true;

        value = prs_value(prs, is_float ? "float" : "int");

        if (is_const && (value->type != AST_INT && value->type != AST_FLOAT))
            is_const = false;

        if (!is_float)
            is_float = ast_is_float(value);

        expr = realloc(expr, (expr_cnt + 2) * sizeof(AST *));
        expr[expr_cnt++] = oper;
        expr[expr_cnt++] = value;
    }

    prs->in_math = false;

    if (is_float && contains_mod) {
        fprintf(stderr, "%s:%zu:%zu: error: modulus operator used where a float result may occur; consider using casts\n", prs->file, first->ln, first->col);
        exit(EXIT_FAILURE);
    }

    if (is_const) {
        size_t oper_cnt = expr_cnt / 2;
        size_t order[oper_cnt];
        size_t order_cnt = 0;

        for (size_t i = 1; i < expr_cnt; i += 2) {
            if (expr[i]->oper.kind != TOK_PLUS && expr[i]->oper.kind != TOK_MINUS)
                order[order_cnt++] = i;
        }

        for (size_t i = 1; i < expr_cnt; i += 2) {
            if (expr[i]->oper.kind == TOK_PLUS || expr[i]->oper.kind == TOK_MINUS)
                order[order_cnt++] = i;
        }

        AST *left;
        AST *right;
        size_t oper_pos;
        int j;
        long double result = 0;

        for (size_t i = 0; i < oper_cnt; i++) {
            oper_pos = order[i];
            oper = expr[oper_pos];
            oper->active = false;

            j = 0;
            left = expr[oper_pos - 1];
            while (!left->active)
                left = expr[oper_pos + --j];

            j = 0;
            right = expr[oper_pos + 1];
            while (!right->active)
                right = expr[oper_pos + ++j];

            assert(left != NULL);
            assert(right != NULL);

            switch (oper->oper.kind) {
                case TOK_PLUS:
                    result = left->data.digit + right->data.digit;
                    break;
                case TOK_MINUS:
                    result = left->data.digit - right->data.digit;
                    break;
                case TOK_STAR:
                    result = left->data.digit * right->data.digit;
                    break;
                case TOK_SLASH:
                    result = left->data.digit / right->data.digit;
                    break;
                default:
                    result = (int)left->data.digit % (int)right->data.digit;
                    break;
            }

            left->data.digit = result;
            left->type = digit_is_float(result) ? AST_FLOAT : AST_INT;
            
            if (!is_float)
                is_float = ast_is_float(left);

            right->active = false;
        }

        // Start at 1 so it doesn't free AST *first yet
        for (size_t i = 1; i < expr_cnt; i++) {
            if (expr[i] != NULL)
                ast_del(expr[i]);
        }

        free(expr);

        AST *ast = ast_init(digit_is_float(result) ? AST_FLOAT : AST_INT, first->scope_def, first->func_def, first->ln, first->col);
        ast->data.digit = result;
        ast_del(first);
        return ast;
    }

    AST *ast = ast_init(AST_MATH, first->scope_def, first->func_def, first->ln, first->col);
    ast->math.expr = expr;
    ast->math.expr_cnt = expr_cnt;
    return ast;
}

AST *prs_value(Prs *prs, char *type) {
    AST *value = prs_stmt(prs);

    if (type == NULL)
        goto check_math;

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
                fprintf(stderr, "%s:%zu:%zu: error: function '%s' doesn't return a value\n", prs->file, value->ln, value->col, value->call.name);
                exit(EXIT_FAILURE);
            }
            break;
        }
        case AST_MATH: break;
        case AST_STR:
        case AST_ARR_LST:
            if (type == NULL || strchr(type, '*') == NULL) {
                fprintf(stderr, "%s:%zu:%zu: error: invalid value '%s'\n", prs->file, value->ln, value->col, ast_types[value->type]);
                exit(EXIT_FAILURE);
            }
            break;
        case AST_DEREF: {
            if (type == NULL) {
                fprintf(stderr, "%s:%zu:%zu: error: invalid value '%s'\n", prs->file, value->ln, value->col, ast_types[value->type]);
                exit(EXIT_FAILURE);
            }

            AST *sym = sym_find(AST_ASSIGN, value->scope_def, value->var.name);
            char *base_type = strdup(sym->assign.type);
            base_type[strlen(base_type) - 1] = '\0';

            if ((strchr(type, '*') == NULL && strchr(base_type, '*') != NULL) || (strchr(type, '*') != NULL && strchr(base_type, '*') == NULL)) {
                fprintf(stderr, "%s:%zu:%zu: error: invalid value from derefence of variable '%s' from type '%s' to '%s'\n", prs->file, value->ln, value->col, value->var.name, sym->assign.type, base_type);
                exit(EXIT_FAILURE);
            }

            free(base_type);
            break;
        }
        case AST_REF:
            if (type == NULL || strchr(type, '*') == NULL) {
                fprintf(stderr, "%s:%zu:%zu: error: invalid value '%s'\n", prs->file, value->ln, value->col, ast_types[value->type]);
                exit(EXIT_FAILURE);
            }
            break;
        default:
            fprintf(stderr, "%s:%zu:%zu: error: invalid value '%s'\n", prs->file, value->ln, value->col, ast_types[value->type]);
            exit(EXIT_FAILURE);
    }

check_math:
    if (!prs->in_math && (prs->tok->type == TOK_PLUS || prs->tok->type == TOK_MINUS || prs->tok->type == TOK_STAR || prs->tok->type == TOK_SLASH || prs->tok->type == TOK_PERCENT))
        return prs_math(prs, value);

    return value;
}

AST *prs_id_func(Prs *prs, char *name, char *type, size_t ln, size_t col) {
    AST *sym = sym_find(AST_FUNC, "<global>", name);
    if (sym != NULL) {
        fprintf(stderr, "%s:%zu:%zu: error: redefinition of function '%s'; first defined at %zu:%zu\n", prs->file, ln, col, name, sym->ln, sym->col);
        exit(EXIT_FAILURE);
    }

    if (strcmp(name, "main") == 0 && strcmp(type, "void") != 0) {
        fprintf(stderr, "%s:%zu:%zu: error: entrypoint 'main' must have type 'void'\n", prs->file, ln, col);
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
        if (params_cnt > 0)
            prs_eat(prs, TOK_COMMA);

        param = prs_stmt(prs);
        if (param->type != AST_ASSIGN) {
            fprintf(stderr, "%s:%zu:%zu: error: expected parameter definition but found '%s'\n", prs->file, param->ln, param->col, ast_types[param->type]);
            exit(EXIT_FAILURE);
        } else if (param->assign.value != NULL) {
            fprintf(stderr, "%s:%zu:%zu: error: assigning parameter '%s' outside of function body\n", prs->file, param->ln, param->col, param->assign.name);
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
        fprintf(stderr, "%s:%zu:%zu: error: missing return statement in function '%s' of type '%s'\n", prs->file, ln, col, name, type);
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

AST *prs_id_assign(Prs *prs, char *name, char *type, bool mut, size_t ln, size_t col) {
    AST *sym = sym_find(AST_ASSIGN, prs->cur_scope, name);
    if (type != NULL && sym != NULL) {
        fprintf(stderr, "%s:%zu:%zu: error: redefinition of variable '%s'; first defined at %zu:%zu\n", prs->file, ln, col, name, sym->ln, sym->col);
        exit(EXIT_FAILURE);
    }

    if (sym != NULL && type == NULL && !sym->assign.mut) {
        fprintf(stderr, "%s:%zu:%zu: error: reassigning immutable variable '%s'\n", prs->file, ln, col, name);
        exit(EXIT_FAILURE);
    } else if (sym != NULL && type != NULL)
        mut = sym->assign.mut;

    AST *ast = ast_init(AST_ASSIGN, prs->cur_scope, prs->cur_func, ln, col);
    ast->assign.name = name;
    ast->assign.type = type;
    ast->assign.rbp = NULL;
    ast->assign.mut = mut;

    size_t cap = 0;
    AST *value = NULL;

    if (prs->tok->type == TOK_EQUAL) {
prs_id_assign_value:
        prs_eat(prs, TOK_EQUAL);
        value = prs_value(prs, type == NULL ? sym->assign.type : type);

        char *check_type = type != NULL ? type : sym->assign.type;
        size_t check_cap = type != NULL ? cap : sym->assign.arr_cap;
        bool check_mut = type != NULL ? mut : sym->assign.mut;

        if (strchr(check_type, '*') != NULL) {
            if (check_cap > 0) {
                if (value->type != AST_STR && value->type != AST_ARR_LST) {
                    fprintf(stderr, "%s:%zu:%zu: error: invalid value '%s' for array of type '%s'\n", prs->file, ln, col, ast_types[value->type], type);
                    exit(EXIT_FAILURE);
                } else if (value->type == AST_STR && strlen(value->data.str) + 1 >= check_cap) {
                    fprintf(stderr, "%s:%zu:%zu: error: value '%s' exceeds array capacity of size %zu\n", prs->file, ln, col, ast_types[value->type], check_cap);
                    exit(EXIT_FAILURE);
                } else if (value->type == AST_ARR_LST && value->arr_lst.items_cnt > check_cap) {
                    fprintf(stderr, "%s:%zu:%zu: error: value '%s' exceeds array capacity of size %zu\n", prs->file, ln, col, ast_types[value->type], check_cap);
                    exit(EXIT_FAILURE);
                }
            } else if (value->type == AST_REF) {
                AST *ref_sym = sym_find(AST_ASSIGN, value->scope_def, value->ref.name);

                char *base_type = strdup(check_type);
                base_type[strlen(base_type) - 1] = '\0';

                if (strcmp(ref_sym->assign.type, base_type) != 0) {
                    fprintf(stderr, "%s:%zu:%zu: error: incompatible pointer conversion; reference to variable '%s' is type '%s*' converting to '%s'\n", prs->file, ln, col, value->ref.name, ref_sym->assign.type, check_type);
                    exit(EXIT_FAILURE);
                } else if (check_mut && !ref_sym->assign.mut) {
                    fprintf(stderr, "%s:%zu:%zu: error: conflicting kinds of immutability; converting immutable reference to variable '%s' to a mutable reference\n", prs->file, ln, col, value->ref.name);
                    exit(EXIT_FAILURE);
                }

                free(base_type);

                // TODO: do we need more checks here?
            }
        }
    } else if (prs->tok->type == TOK_LSQUARE) {
        prs_eat(prs, TOK_LSQUARE);

        if (prs->tok->type != TOK_INT) {
            fprintf(stderr, "%s:%zu:%zu: error: invalid array capacity, expected an int\n", prs->file, prs->tok->ln, prs->tok->col);
            exit(EXIT_FAILURE);
        }

        char *endptr;
        long arr_cap = strtol(prs->tok->value, &endptr, 10);

        if (endptr == prs->tok->value || errno == ERANGE) {
            fprintf(stderr, "%s:%zu:%zu: error: digit conversion failed: %s\n", prs->file, ln, col, strerror(errno));
            exit(EXIT_FAILURE);
        } else if (arr_cap < 1) {
            fprintf(stderr, "%s:%zu:%zu: error: arrays must have a size of at least 1\n", prs->file, ln, col);
            exit(EXIT_FAILURE);
        }

        cap = (size_t)arr_cap;
        prs_eat(prs, TOK_INT);
        prs_eat(prs, TOK_RSQUARE);

        ast->assign.type = realloc(ast->assign.type, (strlen(ast->assign.type) + 2) * sizeof(char));
        strcat(ast->assign.type, "*");

        if (prs->tok->type == TOK_EQUAL)
            goto prs_id_assign_value;
    }

    ast->assign.value = value;
    ast->assign.arr_cap = cap;

    if (sym == NULL && type != NULL)
        sym_append(ast);

    return ast;
}

AST *prs_id_ret(Prs *prs, size_t ln, size_t col) {
    AST *sym = sym_find(AST_FUNC, "<global>", prs->cur_func);
    if (sym == NULL) {
        fprintf(stderr, "%s:%zu:%zu: error: invalid statement 'Return' outside of a function\n", prs->file, ln, col);
        exit(EXIT_FAILURE);
    }

    if (strcmp(sym->func.type, "void") == 0 && prs->tok->type != TOK_SEMI) {
        fprintf(stderr, "%s:%zu:%zu: error: unexpected return value in function '%s' of type 'void'\n", prs->file, prs->tok->ln, prs->tok->col, prs->cur_func);
        exit(EXIT_FAILURE);
    } else if (strcmp(sym->func.type, "void") != 0 && prs->tok->type == TOK_SEMI) {
        fprintf(stderr, "%s:%zu:%zu: error: missing return value in function '%s' of type '%s'\n", prs->file, prs->tok->ln, prs->tok->col, prs->cur_func, sym->func.type);
        exit(EXIT_FAILURE);
    }

    AST *ast = ast_init(AST_RET, prs->cur_scope, prs->cur_func, ln, col);
    ast->ret.value = prs->tok->type == TOK_SEMI ? NULL : prs_value(prs, sym->func.type);
    return ast;
}

AST *prs_id_call(Prs *prs, char *name, size_t ln, size_t col) {
    AST *sym = sym_find(AST_FUNC, "<global>", name);
    if (sym == NULL) {
        fprintf(stderr, "%s:%zu:%zu: error: undefined function '%s'\n", prs->file, ln, col, name);
        exit(EXIT_FAILURE);
    }

    AST **args = calloc(1, sizeof(AST *));
    size_t args_cnt = 0;
    AST *param;
    AST *arg;
    prs_eat(prs, TOK_LPAREN);

    while (prs->tok->type != TOK_RPAREN && prs->tok->type != TOK_EOF) {
        if (args_cnt == sym->func.params_cnt) {
            fprintf(stderr, "%s:%zu:%zu: error: excessive arguments in call to function '%s'; expected %zu but found %zu\n", prs->file, ln, col, name, sym->func.params_cnt, args_cnt);
            exit(EXIT_FAILURE);
        } else if (args_cnt > 0)
            prs_eat(prs, TOK_COMMA);

        param = sym->func.params[args_cnt];
        arg = prs_value(prs, param->assign.type);

        if (arg->type == AST_VAR) {
            AST *arg_sym = sym_find(AST_ASSIGN, arg->scope_def, arg->var.name);

            if (param->assign.mut && !arg_sym->assign.mut) {
                fprintf(stderr, "%s:%zu:%zu: error: conflicting kinds of mutability in argument %zu of call to function '%s'\n", prs->file, arg->ln, arg->col, args_cnt, name);
                exit(EXIT_FAILURE);
            }
        } else if (arg->type == AST_REF) {
            AST *arg_sym = sym_find(AST_ASSIGN, arg->scope_def, arg->ref.name);

            if (param->assign.mut && !arg_sym->assign.mut) {
                fprintf(stderr, "%s:%zu:%zu: error: conflicting kinds of mutability in argument %zu of call to function '%s'\n", prs->file, arg->ln, arg->col, args_cnt, name);
                exit(EXIT_FAILURE);
            }
        }

        args = realloc(args, (args_cnt + 1) * sizeof(AST *));
        args[args_cnt++] = arg;
    }

    prs_eat(prs, TOK_RPAREN);

    if (args_cnt < sym->func.params_cnt) {
        fprintf(stderr, "%s:%zu:%zu: error: missing arguments in call to function '%s'; expected %zu but found %zu\n", prs->file, ln, col, name, sym->func.params_cnt, args_cnt);
        exit(EXIT_FAILURE);
    }

    AST *ast = ast_init(AST_CALL, prs->cur_scope, prs->cur_func, ln, col);
    ast->call.name = name;
    ast->call.args = args;
    ast->call.args_cnt = args_cnt;
    return ast;
}

AST **prs_cond(Prs *prs, size_t *cnt, bool ignore_paren) {
    AST **exprs = calloc(1, sizeof(AST *));
    size_t exprs_cnt = 0;
    AST *oper;
    AST *left;
    AST *right;

    if (!ignore_paren)
        prs_eat(prs, TOK_LPAREN);

    while (prs->tok->type != TOK_RPAREN && prs->tok->type != TOK_SEMI && prs->tok->type != TOK_EOF) {
        if (exprs_cnt > 0 && (prs->tok->type == TOK_AND || prs->tok->type == TOK_OR)) {
            oper = ast_init(AST_OPER, prs->cur_scope, prs->cur_func, prs->tok->ln, prs->tok->col);
            oper->oper.kind = prs_eat(prs, prs->tok->type);
            exprs = realloc(exprs, (exprs_cnt + 1) * sizeof(AST *));
            exprs[exprs_cnt++] = oper;
        }

        left = prs_value(prs, NULL);

        if (prs->tok->type != TOK_LT && prs->tok->type != TOK_LTE && prs->tok->type != TOK_GT && prs->tok->type != TOK_GTE && prs->tok->type != TOK_NOT_EQ && prs->tok->type != TOK_EQ_EQ) {
            fprintf(stderr, "%s:%zu:%zu: error: invalid logical operator '%s'\n", prs->file, prs->tok->ln, prs->tok->col, tok_types[prs->tok->type]);
            exit(EXIT_FAILURE);
        }

        oper = ast_init(AST_OPER, prs->cur_scope, prs->cur_func, prs->tok->ln, prs->tok->col);
        oper->oper.kind = prs_eat(prs, prs->tok->type);

        right = prs_value(prs, NULL);

        exprs = realloc(exprs, (exprs_cnt + 3) * sizeof(AST *));
        exprs[exprs_cnt++] = left;
        exprs[exprs_cnt++] = oper;
        exprs[exprs_cnt++] = right;
    }

    if (!ignore_paren)
        prs_eat(prs, TOK_RPAREN);

    *cnt = exprs_cnt;
    return exprs;
}

AST *prs_id_if(Prs *prs, size_t ln, size_t col) {
    AST *ast = ast_init(AST_IF_ELSE, prs->cur_scope, prs->cur_func, ln, col);
    ast->if_else.exprs = prs_cond(prs, &ast->if_else.exprs_cnt, false);

    char *old_scope = strdup(prs->cur_scope);
    prs->cur_scope = realloc(prs->cur_scope, (strlen(prs->cur_scope) + 64) * sizeof(char));
    sprintf(prs->cur_scope, "%s-if:%zu:%zu", old_scope, prs->tok->ln, prs->tok->col);

    ast->if_else.body = prs_body(prs, &ast->if_else.body_cnt, true);

    if (strcmp(prs->tok->value, "else") == 0) {
        prs_eat(prs, TOK_ID);
        sprintf(prs->cur_scope, "%s-else:%zu:%zu", old_scope, prs->tok->ln, prs->tok->col);
        ast->if_else.else_body = prs_body(prs, &ast->if_else.else_body_cnt, true);
    } else
        ast->if_else.else_body = NULL;

    prs->cur_scope = realloc(prs->cur_scope, (strlen(old_scope) + 1) * sizeof(char));
    strcpy(prs->cur_scope, old_scope);
    free(old_scope);
    return ast;
}

AST *prs_id_quick_math(Prs *prs, char *name, size_t ln, size_t col, bool is_deref) {
    AST *sym = sym_find(AST_ASSIGN, prs->cur_scope, name);
    if (sym == NULL) {
        fprintf(stderr, "%s:%zu:%zu: error: undefined variable '%s'\n", prs->file, ln, col, name);
        exit(EXIT_FAILURE);
    } else if (!sym->assign.mut) {
        fprintf(stderr, "%s:%zu:%zu: error: reassigning immutable variable '%s'\n", prs->file, ln, col, name);
        exit(EXIT_FAILURE);
    }

    AST **expr = calloc(3, sizeof(AST *));

    if (is_deref) {
        expr[0] = ast_init(AST_DEREF, prs->cur_scope, prs->cur_func, ln, col);
        expr[0]->deref.name = strdup(name);
        expr[0]->deref.value = NULL;
    } else {
        expr[0] = ast_init(AST_VAR, prs->cur_scope, prs->cur_func, ln, col);
        expr[0]->var.name = strdup(name);
    }

    expr[1] = ast_init(AST_OPER, prs->cur_scope, prs->cur_func, ln, col);

    TokType type;
    if (prs->tok->type == TOK_PLUS_EQ)
        type = TOK_PLUS;
    else if (prs->tok->type == TOK_MINUS_EQ)
        type = TOK_MINUS;
    else if (prs->tok->type == TOK_STAR_EQ)
        type = TOK_STAR;
    else if (prs->tok->type == TOK_SLASH_EQ)
        type = TOK_SLASH;
    else {
        if (strcmp(sym->assign.type, "float") == 0) {
            fprintf(stderr, "%s:%zu:%zu: error: modulus operator used where a float result may occur; consider using casts\n", prs->file, ln, col);
            exit(EXIT_FAILURE);
        }

        type = TOK_PERCENT;
    }

    expr[1]->oper.kind = type;
    prs_eat(prs, prs->tok->type);
    expr[2] = prs_value(prs, sym->assign.type);

    AST *value = ast_init(AST_MATH, prs->cur_scope, prs->cur_func, ln, col);
    value->math.expr = expr;
    value->math.expr_cnt = 3;

    AST *ast;

    if (is_deref) {
        ast = ast_init(AST_DEREF, prs->cur_scope, prs->cur_func, ln, col);
        ast->deref.name = name;
        ast->deref.value = value;
    } else {
        ast = ast_init(AST_ASSIGN, prs->cur_scope, prs->cur_func, ln, col);
        ast->assign.name = name;
        ast->assign.type = NULL;
        ast->assign.rbp = NULL;
        ast->assign.value = value;
    }

    return ast;
}

AST *prs_id_while(Prs *prs, char *id, size_t ln, size_t col) {
    bool do_first = strcmp(id, "do") == 0 ? true : false;
    free(id);

    AST *ast = ast_init(AST_WHILE, prs->cur_scope, prs->cur_func, ln, col);
    char *old_scope = strdup(prs->cur_scope);

    prs->cur_scope = realloc(prs->cur_scope, (strlen(prs->cur_scope) + 64) * sizeof(char));
    sprintf(prs->cur_scope, "%s-while:%zu:%zu", old_scope, prs->tok->ln, prs->tok->col);

    AST **body;
    size_t body_cnt;

    if (do_first) {
        body = prs_body(prs, &body_cnt, true);

        if (prs->tok->type != TOK_ID) {
            fprintf(stderr, "%s:%zu:%zu: error: expected identifier 'while' following 'do' statement but found '%s'\n", prs->file, prs->tok->ln, prs->tok->col, tok_types[prs->tok->type]);
            exit(EXIT_FAILURE);
        } else if (strcmp(prs->tok->value, "while") != 0) {
            fprintf(stderr, "%s:%zu:%zu: error: expected identifier 'while' following 'do' statement but found '%s'\n", prs->file, prs->tok->ln, prs->tok->col, prs->tok->value);
            exit(EXIT_FAILURE);
        }

        prs_eat(prs, TOK_ID);
        ast->while_.exprs = prs_cond(prs, &ast->while_.exprs_cnt, false);
        prs_eat(prs, TOK_SEMI);
    } else {
        ast->while_.exprs = prs_cond(prs, &ast->while_.exprs_cnt, false);
        body = prs_body(prs, &body_cnt, true);
    }
    
    prs->cur_scope = realloc(prs->cur_scope, (strlen(old_scope) + 1) * sizeof(char));
    strcpy(prs->cur_scope, old_scope);
    free(old_scope);

    ast->while_.body = body;
    ast->while_.body_cnt = body_cnt;
    ast->while_.do_first = do_first;
    return ast;
}

AST *prs_id_for(Prs *prs, size_t ln, size_t col) {
    char *old_scope = strdup(prs->cur_scope);
    prs->cur_scope = realloc(prs->cur_scope, (strlen(prs->cur_scope) + 64) * sizeof(char));
    sprintf(prs->cur_scope, "%s-for:%zu:%zu", old_scope, prs->tok->ln, prs->tok->col);

    prs_eat(prs, TOK_LPAREN);

    AST *init = prs_stmt(prs);
    if (init->type != AST_ASSIGN) {
        fprintf(stderr, "%s:%zu:%zu: error: expected assignment in for loop but found '%s'\n", prs->file, init->ln, init->col, ast_types[init->type]);
        exit(EXIT_FAILURE);
    }

    prs_eat(prs, TOK_SEMI);

    size_t cond_cnt;
    AST **cond = prs_cond(prs, &cond_cnt, true);

    prs_eat(prs, TOK_SEMI);

    AST *math = prs_stmt(prs);
    if (math->type != AST_ASSIGN) {
        fprintf(stderr, "%s:%zu:%zu: error: expected assignment in for loop but found '%s'\n", prs->file, math->ln, math->col, ast_types[math->type]);
        exit(EXIT_FAILURE);
    }

    prs_eat(prs, TOK_RPAREN);

    size_t body_cnt;
    AST **body = prs_body(prs, &body_cnt, true);

    prs->cur_scope = realloc(prs->cur_scope, (strlen(old_scope) + 1) * sizeof(char));
    strcpy(prs->cur_scope, old_scope);
    free(old_scope);

    AST *ast = ast_init(AST_FOR, prs->cur_scope, prs->cur_func, ln, col);
    ast->for_.init = init;
    ast->for_.cond = cond;
    ast->for_.cond_cnt = cond_cnt;
    ast->for_.math = math;
    ast->for_.body = body;
    ast->for_.body_cnt = body_cnt;
    return ast;
}

AST *prs_id_subscr(Prs *prs, char *name, size_t ln, size_t col) {
    AST *sym = sym_find(AST_ASSIGN, prs->cur_scope, name);
    if (sym == NULL) {
        fprintf(stderr, "%s:%zu:%zu: error: undefined variable '%s'\n", prs->file, ln, col, name);
        exit(EXIT_FAILURE);
    }

    prs_eat(prs, TOK_LSQUARE);
    AST *index = prs_value(prs, NULL);

    switch (index->type) {
        case AST_INT:
        case AST_VAR:
        case AST_CALL:
        case AST_MATH:
        case AST_SUBSCR: break;
        default:
            fprintf(stderr, "%s:%zu:%zu: error: invalid array index of type '%s'\n", prs->file, index->ln, index->col, ast_types[index->type]);
            exit(EXIT_FAILURE);
    }

    prs_eat(prs, TOK_RSQUARE);

    AST *ast = ast_init(AST_SUBSCR, prs->cur_scope, prs->cur_func, ln, col);
    ast->subscr.name = name;
    ast->subscr.index = index;

    if (prs->tok->type == TOK_EQUAL) {
        if (!sym->assign.mut) {
            fprintf(stderr, "%s:%zu:%zu: error: reassigning immutable variable '%s'\n", prs->file, ln, col, name);
            exit(EXIT_FAILURE);
        }

        char *base_type = strdup(sym->assign.type);
        base_type[strlen(base_type) - 1] = '\0';

        prs_eat(prs, TOK_EQUAL);
        ast->subscr.value = prs_value(prs, base_type);
        free(base_type);
    } else
        ast->subscr.value = NULL;

    return ast;
}

AST *prs_id(Prs *prs) {
    size_t ln = prs->tok->ln;
    size_t col = prs->tok->col;
    bool mut = false;

    char *id = strdup(prs->tok->value);
    prs_eat(prs, TOK_ID);

    if (strcmp(id, "mut") == 0) {
        id = realloc(id, (strlen(prs->tok->value) + 1) * sizeof(char));
        strcpy(id, prs->tok->value);
        prs_eat(prs, TOK_ID);
        mut = true;
    }

    if (is_type(id)) {
        while (prs->tok->type == TOK_STAR) {
            id = realloc(id, (strlen(id) + 2) * sizeof(char));
            strcat(id, "*");
            prs_eat(prs, TOK_STAR);
        }

        char *name = strdup(prs->tok->value);
        prs_eat(prs, TOK_ID);

        if (prs->tok->type == TOK_LPAREN)
            return prs_id_func(prs, name, id, ln, col);

        return prs_id_assign(prs, name, id, mut, ln, col);
    }

    if (mut) {
        fprintf(stderr, "%s:%zu:%zu: error: expected variable declaration following 'mut'\n", prs->file, ln, col);
        exit(EXIT_FAILURE);
    }
    
    if (strcmp(id, "return") == 0) {
        free(id);
        return prs_id_ret(prs, ln, col);
    } else if (strcmp(id, "if") == 0) {
        free(id);
        return prs_id_if(prs, ln, col);
    } else if (strcmp(id, "while") == 0 || strcmp(id, "do") == 0)
        return prs_id_while(prs, id, ln, col);
    else if (strcmp(id, "for") == 0) {
        free(id);
        return prs_id_for(prs, ln, col);
    } else if (prs->tok->type == TOK_EQUAL)
        return prs_id_assign(prs, id, NULL, mut, ln, col);
    else if (prs->tok->type == TOK_LPAREN)
        return prs_id_call(prs, id, ln, col);
    else if (prs->tok->type == TOK_PLUS_EQ || prs->tok->type == TOK_MINUS_EQ || prs->tok->type == TOK_STAR_EQ || prs->tok->type == TOK_SLASH_EQ || prs->tok->type == TOK_PERCENT_EQ)
        return prs_id_quick_math(prs, id, ln, col, false);
    else if (prs->tok->type == TOK_LSQUARE)
        return prs_id_subscr(prs, id, ln, col);
    else if (sym_find(AST_ASSIGN, prs->cur_scope, id) != NULL) {
        AST *ast = ast_init(AST_VAR, prs->cur_scope, prs->cur_func, ln, col);
        ast->var.name = id;
        return ast;
    }

    fprintf(stderr, "%s:%zu:%zu: error: unknown identifier '%s'\n", prs->file, ln, col, id);
    exit(EXIT_FAILURE);
}

AST *prs_data(Prs *prs) {
    AST *ast;
    
    if (prs->tok->type == TOK_STR) {
        ast = ast_init(AST_STR, prs->cur_scope, prs->cur_func, prs->tok->ln, prs->tok->col);
        ast->data.str = strdup(prs->tok->value);
    } else {
        ast = ast_init(prs->tok->type == TOK_INT ? AST_INT : AST_FLOAT, prs->cur_scope, prs->cur_func, prs->tok->ln, prs->tok->col);
        char *endptr;
        ast->data.digit = strtold(prs->tok->value, &endptr);

        if (endptr == prs->tok->value || errno == ERANGE) {
            fprintf(stderr, "%s:%zu:%zu: error: digit conversion failed: %s\n", prs->file, prs->tok->ln, prs->tok->col, strerror(errno));
            exit(EXIT_FAILURE);
        }
    }

    prs_eat(prs, prs->tok->type);
    return ast;
}

AST *prs_arr_lst(Prs *prs) {
    AST *ast = ast_init(AST_ARR_LST, prs->cur_scope, prs->cur_func, prs->tok->ln, prs->tok->col);
    AST **items = calloc(1, sizeof(AST *));
    size_t items_cnt = 0;
    prs_eat(prs, TOK_LBRACE);

    while (prs->tok->type != TOK_RBRACE && prs->tok->type != TOK_EOF) {
        if (items_cnt > 0)
            prs_eat(prs, TOK_COMMA);

        items = realloc(items, (items_cnt + 1) * sizeof(AST *));
        items[items_cnt++] = prs_value(prs, NULL);
        // TODO: probably check for pointers here
    }

    prs_eat(prs, TOK_RBRACE);

    ast->arr_lst.items = items;
    ast->arr_lst.items_cnt = items_cnt;
    return ast;
}

AST *prs_deref(Prs *prs) {
    size_t ln = prs->tok->ln;
    size_t col = prs->tok->col;
    prs_eat(prs, TOK_STAR);

    char *name = strdup(prs->tok->value);
    prs_eat(prs, TOK_ID);

    AST *sym = sym_find(AST_ASSIGN, prs->cur_scope, name);
    if (sym == NULL) {
        fprintf(stderr, "%s:%zu:%zu: error: undefined variable '%s'\n", prs->file, ln, col, name);
        exit(EXIT_FAILURE);
    } else if (strchr(sym->assign.type, '*') == NULL) {
        fprintf(stderr, "%s:%zu:%zu: error: dereferencing non pointer variable '%s' of type '%s'\n", prs->file, ln, col, name, sym->assign.type);
        exit(EXIT_FAILURE);
    }

    if (prs->tok->type == TOK_PLUS_EQ || prs->tok->type == TOK_MINUS_EQ || prs->tok->type == TOK_STAR_EQ || prs->tok->type == TOK_SLASH_EQ || prs->tok->type == TOK_PERCENT_EQ)
        return prs_id_quick_math(prs, name, ln, col, true);

    AST *ast = ast_init(AST_DEREF, prs->cur_scope, prs->cur_func, ln, col);
    ast->deref.name = name;

    AST *value = NULL;

    if (prs->tok->type == TOK_EQUAL) {
        if (!sym->assign.mut) {
            fprintf(stderr, "%s:%zu:%zu: error: reassigning immutable variable '%s'\n", prs->file, ln, col, name);
            exit(EXIT_FAILURE);
        }

        prs_eat(prs, TOK_EQUAL);

        char *base_type = strdup(sym->assign.type);
        base_type[strlen(base_type) - 1] = '\0';

        value = prs_value(prs, base_type);
        free(base_type);
    }

    ast->deref.value = value;
    return ast;
}

AST *prs_amp(Prs *prs) {
    size_t ln = prs->tok->ln;
    size_t col = prs->tok->col;
    prs_eat(prs, TOK_AMP);

    char *name = strdup(prs->tok->value);
    prs_eat(prs, TOK_ID);

    AST *sym = sym_find(AST_ASSIGN, prs->cur_scope, name);
    if (sym == NULL) {
        fprintf(stderr, "%s:%zu:%zu: error: undefined variable '%s'\n", prs->file, ln, col, name);
        exit(EXIT_FAILURE);
    }

    AST *ast = ast_init(AST_REF, prs->cur_scope, prs->cur_func, ln, col);
    ast->ref.name = name;
    return ast;
}

AST *prs_stmt(Prs *prs) {
    switch (prs->tok->type) {
        case TOK_ID: return prs_id(prs);
        case TOK_INT:
        case TOK_FLOAT:
        case TOK_STR: return prs_data(prs);
        case TOK_LBRACE: return prs_arr_lst(prs);
        case TOK_STAR: return prs_deref(prs);
        case TOK_AMP: return prs_amp(prs);
        default:
            fprintf(stderr, "%s:%zu:%zu: error: invalid statement '%s'\n", prs->file, prs->tok->ln, prs->tok->col, tok_types[prs->tok->type]);
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
            fprintf(stderr, "%s:%zu:%zu: error: invalid statement '%s' outside of a function\n", prs->file, stmt->ln, stmt->col, ast_types[stmt->type]);
            exit(EXIT_FAILURE);
        } else if (stmt->type == AST_ASSIGN) {
            if (stmt->assign.type == NULL) {
                fprintf(stderr, "%s:%zu:%zu: error: assigning variable '%s' outside of a function\n", prs->file, stmt->ln, stmt->col, stmt->assign.name);
                exit(EXIT_FAILURE);
            }

            prs_eat(prs, TOK_SEMI);
        }

        asts = realloc(asts, (asts_cnt + 1) * sizeof(AST *));
        asts[asts_cnt++] = stmt;
    }

    if (sym_find(AST_FUNC, "<global>", "main") == NULL) {
        fprintf(stderr, "%s: error: missing entrypoint 'main'\n", prs->file);
        exit(EXIT_FAILURE);
    }

    prs_del(prs);

    AST *root = ast_init(AST_ROOT, "<internal>", "<internal>", 0, 0);
    root->root.asts = asts;
    root->root.asts_cnt = asts_cnt;
    return root;
}
