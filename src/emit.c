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

const char *words[] = {
    [QWORD] = "qword",
    [DWORD] = "dword",
    [WORD] = "word",
    [BYTE] = "byte"
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
size_t float_cnt = 0;
char *func_data;
char *sect_data;

void globs_reset() {
    rsp = rsp_cap = float_cnt = 0;
    func_data = realloc(func_data, 1 * sizeof(char));
    func_data[0] = '\0';
}

size_t float_init(size_t bits, long double value) {
    char *data = calloc(128, sizeof(char));

    if (bits == 32)
        sprintf(data, ".f%zu:\n"
                      "    dd %f\n", float_cnt, (float)value);
    else
        sprintf(data, ".f%zu:\n"
                      "    dq %Lf\n", float_cnt, value);

    func_data = realloc(func_data, (strlen(func_data) + strlen(data) + 1) * sizeof(char));
    strcat(func_data, data);
    free(data);
    return float_cnt++;
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
    sect_data = calloc(1, sizeof(char));

    for (size_t i = 0; i < ast->root.asts_cnt; i++) {
        next = emit_ast(ast->root.asts[i]);
        code = realloc(code, (strlen(code) + strlen(next) + 1) * sizeof(char));
        strcat(code, next);
        free(next);
    }

    if (strlen(sect_data) > 1) {
        code = realloc(code, (strlen(code) + strlen(sect_data) + 32) * sizeof(char));
        strcat(code, "    section .data\n");
        strcat(code, sect_data);
    }

    free(func_data);
    free(sect_data);
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
            strcat(body, "    leave\n");
        else
            strcat(body, "    pop rbp\n");

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

char *emit_call(AST *ast);

char *emit_call_arg(AST *ast, char *param_type, char *reg) {
    char *code;

    switch (ast->type) {
        case AST_INT:
            code = calloc(strlen(reg) + 128, sizeof(char));
            
            if (strcmp(param_type, "float") == 0)
                sprintf(code, "    mov eax, %d\n"
                              "    cvtsi2ss %s, eax\n", (int)ast->data.digit, reg);
            else
                sprintf(code, "    mov %s, %d\n", reg, (int)ast->data.digit);
            break;
        case AST_FLOAT:
            code = calloc(strlen(reg) + 64, sizeof(char));
            sprintf(code, "    movss %s, dword [.f%zu]\n", reg, float_init(32, ast->data.digit));
            break;
        case AST_VAR: {
            AST *var = sym_find(AST_ASSIGN, ast->scope_def, ast->var.name);
            code = calloc(strlen(reg) + strlen(reg) + 128, sizeof(char));
            
            if (strcmp(param_type, "char") == 0 || strcmp(param_type, "int") == 0) {
                if (strcmp(var->assign.type, "char") == 0)
                    sprintf(code, "    movsx %s, byte %s\n", reg, var->assign.rbp);
                else if (strcmp(var->assign.type, "int") == 0)
                    sprintf(code, "    mov %s, dword %s\n", reg, var->assign.rbp);
                else
                    sprintf(code, "    cvttss2si %s, dword %s\n", reg, var->assign.rbp);
            } else if (strcmp(param_type, "float") == 0) {
                if (strcmp(var->assign.type, "char") == 0)
                    sprintf(code, "    movsx eax, byte %s\n"
                                  "    cvtsi2ss %s, eax\n", var->assign.rbp, reg);
                else if (strcmp(var->assign.type, "int") == 0)
                    sprintf(code, "    cvtsi2ss %s, dword %s\n", reg, var->assign.rbp);
                else
                    sprintf(code, "    movss %s, dword %s\n", reg, var->assign.rbp);
            } else
                sprintf(code, "    mov %s, qword %s\n", reg, var->assign.rbp);
            break;
        }
        case AST_CALL: {
            code = emit_call(ast);
            char *call_type = sym_find(AST_FUNC, "<global>", ast->call.name)->func.type;
            char *store = calloc(strlen(reg) + 64, sizeof(char));

            if (strcmp(param_type, "char") == 0 || strcmp(param_type, "int") == 0) {
                if (strcmp(call_type, "float") == 0)
                    sprintf(store, "    cvttss2si %s, xmm0\n", reg);
                else
                    sprintf(store, "    mov %s, eax\n", reg);
            } else if (strcmp(param_type, "float") == 0) {
                if (strcmp(call_type, "float") == 0)
                    sprintf(store, "    movss %s, xmm0\n", reg);
                else
                    sprintf(store, "    cvtsi2ss %s, eax\n", reg);
            } else
                sprintf(store, "    mov %s, rax\n", reg);

            code = realloc(code, (strlen(code) + strlen(store) + 1) * sizeof(char));
            strcat(code, store);
            free(store);
            break;
        }
        default: assert(false);
    }

    return code;
}

char *emit_call(AST *ast) {
    char *code = calloc(1, sizeof(char));
    char *next;
    char *reg;
    AST *sym = sym_find(AST_FUNC, "<global>", ast->call.name);
    AST *param;
    AST *arg;
    size_t ints = 0;
    size_t floats = 0;

    for (size_t i = 0; i < sym->func.params_cnt; i++) {
        param = sym->func.params[i];
        arg = ast->call.args[i];

        if (strcmp(param->assign.type, "char") == 0 || strcmp(param->assign.type, "int") == 0)
            reg = regs[int_params[ints++]][DWORD];
        else if (strcmp(param->assign.type, "float") == 0)
            reg = regs[XMM][++floats];
        else
            reg = regs[int_params[ints++]][QWORD];

        if (arg->type == AST_CALL && i > 0) {
            char *temp;
            char *store_ints = calloc(1, sizeof(char));
            char *load_ints = calloc(1, sizeof(char));
            char *load;
            AST *temp_param;

            size_t beg_rsp = rsp;

            for (size_t j = 0; j < i; j++) {
                temp_param = sym->func.params[j];
                if (strcmp(temp_param->assign.type, "char") != 0 && strcmp(temp_param->assign.type, "int") != 0)
                    continue;

                temp = calloc(64, sizeof(char));
                sprintf(temp, "    push %s\n", regs[int_params[j]][QWORD]);

                store_ints = realloc(store_ints, (strlen(store_ints) + strlen(temp) + 1) * sizeof(char));
                strcat(store_ints, temp);

                sprintf(temp, "    pop %s\n", regs[int_params[j]][QWORD]);

                load = calloc(strlen(load_ints) + strlen(temp) + 1, sizeof(char));
                strcpy(load, temp);
                strcat(load, load_ints);
                free(load_ints);
                load_ints = load;
                free(temp);
            }

            char *store_floats = calloc(1, sizeof(char));
            char *load_floats = calloc(1, sizeof(char));

            size_t rsp_before_floats = rsp;
            size_t float_rsp_cap = 0;

            while (rsp > rsp_cap + float_rsp_cap)
                float_rsp_cap += SUB_RSP_SIZE;

            for (size_t j = 0; j < i; j++) {
                temp_param = sym->func.params[j];
                if (strcmp(temp_param->assign.type, "float") != 0)
                    continue;

                temp = calloc(64, sizeof(char));
                rsp += 8;
                sprintf(temp, "    movss dword [rbp-%zu], %s\n", rsp, regs[XMM][j + 1]);

                store_ints = realloc(store_ints, (strlen(store_ints) + strlen(temp) + 1) * sizeof(char));
                strcat(store_ints, temp);

                sprintf(temp, "    movss %s, dword [rbp-%zu]\n", regs[XMM][j + 1], rsp);

                load = calloc(strlen(load_ints) + strlen(temp) + 1, sizeof(char));
                strcpy(load, temp);
                strcat(load, load_ints);
                free(load_ints);
                load_ints = load;
                free(temp);
            }

            next = emit_call_arg(arg, param->assign.type, reg);

            temp = calloc(strlen(store_ints) + strlen(store_floats) + strlen(next) + strlen(load_floats) + strlen(load_ints) + 128, sizeof(char));
            
            if (rsp_before_floats > 0)
                sprintf(temp, "%s"
                              "    sub rsp, %zu\n"
                              "%s"
                              "%s"
                              "%s"
                              "    add rsp, %zu\n"
                              "%s", store_ints, rsp_before_floats, store_floats, next, load_floats, rsp_before_floats, load_ints);
            else
                sprintf(temp, "%s"
                              "%s"
                              "%s"
                              "%s"
                              "%s", store_ints, store_floats, next, load_floats, load_ints);

            free(store_ints);
            free(store_floats);
            free(load_ints);
            free(load_floats);

            code = realloc(code, (strlen(code) + strlen(temp) + 1) * sizeof(char));
            strcat(code, temp);
            free(temp);
            free(next);
            rsp = beg_rsp;
            continue;
        }
        
        next = emit_call_arg(arg, param->assign.type, reg);
        code = realloc(code, (strlen(code) + strlen(next) + 1) * sizeof(char));
        strcat(code, next);
        free(next);
    }

    code = realloc(code, (strlen(code) + strlen(ast->call.name) + 32) * sizeof(char));
    strcat(code, "    call ");
    strcat(code, ast->call.name);
    strcat(code, "_\n");
    return code;
}

char *emit_assign(AST *ast) {
    char *code;
    char *sub_rsp = NULL;
    AST *value = ast->assign.value;

    AST *sym = sym_find(AST_ASSIGN, ast->scope_def, ast->assign.name);
    char *type = sym->assign.type;
    char *rbp = sym->assign.rbp;

    if (ast->assign.type != NULL) {
        size_t size;

        if (strcmp(ast->assign.type, "char") == 0)
            size = 1;
        else if (strcmp(ast->assign.type, "int") == 0 || strcmp(ast->assign.type, "float") == 0)
            size = 4;
        else
            size = 8;

        rsp += size;
        rbp = calloc(32, sizeof(char));
        sprintf(rbp, "[rbp-%zu]", rsp);
        sym->assign.rbp = rbp;

        if (rsp > rsp_cap) {
            sub_rsp = calloc(64, sizeof(char));
            sprintf(sub_rsp, "    sub rsp, %zu\n", SUB_RSP_SIZE);
            rsp_cap += SUB_RSP_SIZE;
        }
    }

    if (value == NULL)
        return sub_rsp != NULL ? sub_rsp : calloc(1, sizeof(char));

    switch (value->type) {
        case AST_INT:
            code = calloc(strlen(rbp) + 128, sizeof(char));
            
            if (strcmp(type, "char") == 0)
                sprintf(code, "    mov byte %s, %d\n", rbp, (int)value->data.digit);
            else if (strcmp(type, "int") == 0)
                sprintf(code, "    mov dword %s, %d\n", rbp, (int)value->data.digit);
            else
                sprintf(code, "    mov eax, %d\n"
                              "    cvtsi2ss xmm0, eax\n"
                              "    movss dword %s, xmm0\n", (int)value->data.digit, rbp);
            break;
        case AST_FLOAT:
            code = calloc(strlen(rbp) + 128, sizeof(char));
            sprintf(code, "    movss xmm0, dword [.f%zu]\n"
                          "    movss dword %s, xmm0\n", float_init(32, value->data.digit), rbp);
            break;
        case AST_VAR: {
            AST *var = sym_find(AST_ASSIGN, value->scope_def, value->var.name);
            code = calloc(strlen(rbp) + strlen(var->assign.rbp) + 128, sizeof(char));

            if (strcmp(type, "char") == 0) {
                if (strcmp(var->assign.type, "char") == 0)
                    sprintf(code, "    mov al, byte %s\n"
                                  "    mov byte %s, al\n", var->assign.rbp, rbp);
                else if (strcmp(var->assign.type, "int") == 0)
                    sprintf(code, "    mov eax, dword %s\n"
                                  "    mov byte %s, al\n", var->assign.rbp, rbp);
                else
                    sprintf(code, "    cvttss2si eax, dword %s\n"
                                  "    mov byte %s, al\n", var->assign.rbp, rbp);
            } else if (strcmp(type, "int") == 0) {
                if (strcmp(var->assign.type, "char") == 0)
                    sprintf(code, "    movsx eax, byte %s\n"
                                  "    mov dword %s, eax\n", var->assign.rbp, rbp);
                else if (strcmp(var->assign.type, "int") == 0)
                    sprintf(code, "    mov eax, dword %s\n"
                                  "    mov dword %s, eax\n", var->assign.rbp, rbp);
                else
                    sprintf(code, "    cvttss2si eax, dword %s\n"
                                  "    mov dword %s, eax\n", var->assign.rbp, rbp);
            } else if (strcmp(type, "float") == 0) {
                if (strcmp(var->assign.type, "char") == 0)
                    sprintf(code, "    movsx eax, byte %s\n"
                                  "    cvtsi2ss xmm0, eax\n"
                                  "    movss dword %s, xmm0\n", var->assign.rbp, rbp);
                else if (strcmp(var->assign.type, "int") == 0)
                    sprintf(code, "    cvtsi2ss xmm0, dword %s\n"
                                  "    movss dword %s, xmm0\n", var->assign.rbp, rbp);
                else
                    sprintf(code, "    movss xmm0, dword %s\n"
                                  "    movss dword %s, xmm0\n", var->assign.rbp, rbp);
            } else
                sprintf(code, "    mov rax, qword %s\n"
                              "    mov qword %s, rax\n", var->assign.rbp, rbp);
            break;
        }
        case AST_CALL: {
            code = emit_ast(value);
            char *call_type = sym_find(AST_FUNC, "<global>", value->call.name)->func.type;
            char *temp = calloc(strlen(rbp) + 64, sizeof(char));

            if (strcmp(type, "char") == 0) {
                if (strcmp(call_type, "float") == 0)
                    sprintf(temp, "    cvttss2si eax, xmm0\n"
                                  "    mov byte %s, al\n", rbp);
                else
                    sprintf(temp, "    mov byte %s, al\n", rbp);
            } else if (strcmp(type, "int") == 0) {
                if (strcmp(call_type, "float") == 0)
                    sprintf(temp, "    cvttss2si eax, xmm0\n"
                                  "    mov dword %s, eax\n", rbp);
                else
                    sprintf(temp, "    mov byte %s, eax\n", rbp);
            } else if (strcmp(type, "float") == 0) {
                if (strcmp(call_type, "float") == 0)
                    sprintf(temp, "    movss dword %s, xmm0\n", rbp);
                else
                    sprintf(temp, "    cvtsi2ss xmm0, eax\n"
                                  "    movss dword %s, xmm0\n", rbp);
            } else
                sprintf(temp, "    mov qword %s, rax\n", rbp);

            code = realloc(code, (strlen(code) + strlen(temp) + 1) * sizeof(char));
            strcat(code, temp);
            free(temp);
            break;
        }
        default: assert(false);
    }

    if (sub_rsp != NULL) {
        sub_rsp = realloc(sub_rsp, (strlen(sub_rsp) + strlen(code) + 1) * sizeof(char));
        strcat(sub_rsp, code);
        free(code);
        return sub_rsp;
    }

    return code;
}

char *emit_ret(AST *ast) {
    char *code;
    char *type = sym_find(AST_FUNC, "<global>", ast->func_def)->func.type;
    AST *value = ast->ret.value;
    char ret[80];

    if (rsp > 0)
        strcpy(ret, "    leave\n");
    else
        strcpy(ret, "    pop rbp\n");

    if (strcmp(ast->func.name, "main") == 0)
        strcat(ret, "    mov rax, 60\n"
                    "    xor rdi, rdi\n"
                    "    syscall\n");
    else
        strcat(ret, "    ret\n");

    if (value == NULL)
        return strdup(ret);

    switch (value->type) {
        case AST_INT:
            code = calloc(64, sizeof(char));
            sprintf(code, "    mov eax, %d\n", (int)value->data.digit);

            if (strcmp(type, "float") == 0)
                strcat(code, "    cvtsi2ss xmm0, eax\n");
            break;
        case AST_FLOAT:
            code = calloc(64, sizeof(char));
            sprintf(code, "    movss xmm0, dword [.f%zu]\n", float_init(32, value->data.digit));
            break;
        case AST_VAR: {
            AST *var = sym_find(AST_ASSIGN, value->scope_def, value->var.name);
            code = calloc(strlen(var->assign.rbp) + 128, sizeof(char));

            if (strcmp(type, "char") == 0 || strcmp(type, "int") == 0) {
                if (strcmp(var->assign.type, "char") == 0)
                    sprintf(code, "    movsx eax, byte %s\n", var->assign.rbp);
                else if (strcmp(var->assign.type, "int") == 0)
                    sprintf(code, "    mov eaX, dword %s\n", var->assign.rbp);
                else
                    sprintf(code, "    cvttss2si eax, dword %s\n", var->assign.rbp);
            } else if (strcmp(type, "float") == 0) {
                if (strcmp(var->assign.type, "char") == 0)
                    sprintf(code, "    movsx eax, byte %s\n"
                                  "    cvtsi2ss xmm0, eax\n", var->assign.rbp);
                else if (strcmp(var->assign.type, "int") == 0)
                    sprintf(code, "    cvtsi2ss xmm0, dword %s\n", var->assign.rbp);
                else
                    sprintf(code, "    movss xmm0, dword %s\n", var->assign.rbp);
            } else
                sprintf(code, "    mov rax, qword %s\n", var->assign.rbp);
            break;
        }
        case AST_CALL: {
            code = emit_ast(value);
            char *call_type = sym_find(AST_FUNC, "<global>", value->call.name)->func.type;
            char *temp = calloc(64, sizeof(char));

            if ((strcmp(type, "char") == 0 || strcmp(type, "int") == 0) && strcmp(call_type, "float") == 0)
                strcpy(temp, "    cvttss2si eax, xmm0\n");
            else if (strcmp(type, "float") == 0 && (strcmp(call_type, "char") == 0 || strcmp(call_type, "int") == 0))
                strcpy(temp, "    cvtsi2ss xmm0, eax\n");

            code = realloc(code, (strlen(code) + strlen(temp) + 1) * sizeof(char));
            strcat(code, temp);
            free(temp);
            break;
        }
        default: assert(false);
    }

    code = realloc(code, (strlen(code) + strlen(ret) + 1) * sizeof(char));
    strcat(code, ret);
    return code;
}

char *emit_ast(AST *ast) {
    switch (ast->type) {
        case AST_ROOT: return emit_root(ast);
        case AST_FUNC: return emit_func(ast);
        case AST_CALL: return emit_call(ast);
        case AST_ASSIGN: return emit_assign(ast);
        case AST_RET: return emit_ret(ast);
        default:
            fprintf(stderr, "pcc: Error: Missing backend for '%s'.\n", ast_types[ast->type]);
            exit(EXIT_FAILURE);
    }
}