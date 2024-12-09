#ifndef PARSER_H
#define PARSER_H

#include "token.h"
#include "lexer.h"
#include "ast.h"

typedef struct {
    char *file;
    char *cur_scope;
    char *cur_func;
    Lex *lex;
    Tok *tok;
} Prs;

AST *sym_find(ASTType type, char *scope, char *name);
AST *prs_stmt(Prs *prs);
AST *prs_file(char *file);

#endif