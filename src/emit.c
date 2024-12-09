#include "emit.h"
#include "ast.h"
#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>

#define SUB_RSP_SIZE (size_t)32

extern const char *ast_types[];
extern AST **sym_tab;
extern size_t sym_cnt;

enum {
    RAX,
    RBX,
    RCX,
    RDX,
    RSI,
    RDI,
    RSP,
    RBP,
    R8,
    R9,
    XMM
};

enum {
    QWORD,
    DWORD,
    WORD,
    BYTE
};

const char *regs[][8] = {
    [RAX] = { "rax", "eax", "ax", "al" },
    [RBX] = { "rbx", "ebx", "bx", "bl" },
    [RCX] = { "rcx", "ecx", "cx", "cl" },
    [RDX] = { "rdx", "edx", "dx", "dl" },
    [RSI] = { "rsi", "esi", "si", "sil" },
    [RDI] = { "rdi", "edi", "di", "dil" },
    [RSP] = { "rsp", "esp", "sp", "spl" },
    [RBP] = { "rbp", "ebp", "bp", "bpl" },
    [R8] = { "r8", "r8d", "r8w", "r8b" },
    [R9] = { "r9", "r9d", "r9w", "r9b" },
    [XMM] = { "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7" }
};

const size_t int_params[] = { RDI, RSI, RDX, RCX, R8, R9 };

size_t rsp = 0;
size_t rsp_cap = 0;
char *func_data;
char *sect_data;

void globs_reset() {
    rsp = rsp_cap = 0;
    func_data = realloc(func_data, 1 * sizeof(char));
    func_data[0] = '\0';
}

char *emit_arr(AST **arr, size_t cnt) {
    char *code = calloc(1, sizeof(char));
    char *next;

    for (size_t i = 0; i < cnt; i++) {
        next = emit_ast(arr[i]);
        code = realloc(code, (strlen(code) + strlen(next) + 1) * sizeof(char));
        strcat(code, next);
        free(next);
    }

    return code;
}

char *emit_root(AST *ast) {
    char *code = strdup("    section .text\n"
                        "    global main_\n");
    char *next;

    func_data = calloc(1, sizeof(char));

    for (size_t i = 0; i < ast->root.asts_cnt; i++) {
        next = emit_ast(ast->root.asts[i]);
        code = realloc(code, (strlen(code) + strlen(next) + 1) * sizeof(char));
        strcat(code, next);
        free(next);
    }

    free(func_data);
    free(sym_tab);
    sym_cnt = 0;
    return code;
}

char *emit_func(AST *ast) {
    const char template[] = "%s_:\n"
                            "    push rbp\n"
                            "    mov rbp, rsp\n"
                            "%s"
                            "%s"
                            "%s";

    char *params = calloc(1, sizeof(char));

    if (ast->func.params_cnt > 0) {
        AST *param;
        char *next;
        char *rbp;
        size_t ints = 0;
        size_t floats = 0;

        for (size_t i = 0; i < ast->func.params_cnt; i++) {
            param = ast->func.params[i];
            rbp = calloc(32, sizeof(char));
            next = calloc(128, sizeof(char));

            if (strcmp(param->assign.type, "char") == 0) {
                sprintf(rbp, "[rbp-%zu]", ++rsp);
                sprintf(next, "    mov byte %s, %s\n", rbp, regs[int_params[ints++]][BYTE]);
            } else if (strcmp(param->assign.type, "int") == 0) {
                rsp += 4;
                sprintf(rbp, "[rbp-%zu]", rsp);
                sprintf(next, "    mov dword %s, %s\n", rbp, regs[int_params[ints++]][DWORD]);
            } else if (strcmp(param->assign.type, "float") == 0) {
                rsp += 4;
                sprintf(rbp, "[rbp-%zu]", rsp);
                sprintf(next, "    movss dword %s, %s\n", rbp, regs[XMM][++floats]);
            } else {
                rsp += 8;
                sprintf(rbp, "[rbp-%zu]", rsp);
                sprintf(next, "    mov qword %s, %s\n", rbp, regs[int_params[ints++]][QWORD]);
            }

            param->assign.rbp = rbp;

            params = realloc(params, (strlen(params) + strlen(next) + 1) * sizeof(char));
            strcat(params, next);
            free(next);
        }

        while (rsp > rsp_cap)
            rsp_cap += SUB_RSP_SIZE;

        char *temp = calloc(strlen(params) + 64, sizeof(char));
        sprintf(temp, "    sub rsp, %zu\n%s", rsp_cap, params);
        free(params);
        params = temp;
    }

    char *body = emit_arr(ast->func.body, ast->func.body_cnt);

    if (ast->func.ret == NULL) {
        body = realloc(body, (strlen(body) + 64) * sizeof(char));

        if (rsp > 0)
            strcpy(body, "    leave\n");
        else
            strcpy(body, "    pop rbp\n");

        if (strcmp(ast->func.name, "main") == 0)
            strcat(body, "    mov rax, 60\n"
                         "    xor rdi, rdi\n"
                         "    syscall\n");
        else
            strcat(body, "    ret\n");
    }

    char *code = calloc(strlen(template) + strlen(ast->func.name) + strlen(params) + strlen(body) + strlen(func_data) + 1, sizeof(char));
    sprintf(code, template, ast->func.name, params, body, func_data);
    free(params);
    free(body);

    globs_reset();
    return code;
}

char *emit_ast(AST *ast) {
    switch (ast->type) {
        case AST_ROOT: return emit_root(ast);
        case AST_FUNC: return emit_func(ast);
        default:
            fprintf(stderr, "tcc: Error: Missing backend for '%s'.\n", ast_types[ast->type]);
            exit(EXIT_FAILURE);
    }
}