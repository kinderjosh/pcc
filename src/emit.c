#include "emit.h"
#include "ast.h"
#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <errno.h>

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

const char *regs[][16] = {
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
    [XMM] = { "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7", "xmm8", "xmm9", "xmm10", "xmm11", "xmm12", "xmm13", "xmm14", "xmm15" }
};

const size_t int_params[] = { RDI, RSI, RDX, RCX, R8, R9 };

size_t rsp = 0;
size_t rsp_cap = 0;
size_t float_cnt = 0;
char *func_data;
char *sect_data;
bool float_math_result = false;

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

unsigned int power_of_two(unsigned int n) {
    return 1ULL << n;
}

bool is_power_of_two(unsigned int x) {
    return x != 0 && (x & (x - 1)) == 0;
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

                if (ints > 5)
                    sprintf(next, "    mov al, byte [rbp+%zu]\n"
                                  "    mov byte %s, al\n", (ints - 4) * 8, rbp);
                else
                    sprintf(next, "    mov byte %s, %s\n", rbp, regs[int_params[ints++]][BYTE]);
            } else if (strcmp(param->assign.type, "int") == 0) {
                rsp += 4;
                sprintf(rbp, "[rbp-%zu]", rsp);

                if (ints > 5)
                    sprintf(next, "    mov eax, dword [rbp+%zu]\n"
                                  "    mov dword %s, eax\n", (ints - 4) * 8, rbp);
                else
                    sprintf(next, "    mov dword %s, %s\n", rbp, regs[int_params[ints++]][DWORD]);
            } else if (strcmp(param->assign.type, "float") == 0) {
                rsp += 4;
                sprintf(rbp, "[rbp-%zu]", rsp);

                if (floats > 14)
                    sprintf(next, "    mov eax, dword [rbp+%zu]\n"
                                  "    mov dword %s, eax\n", (ints - 4) * 8, rbp);
                else
                    sprintf(next, "    movss dword %s, %s\n", rbp, regs[XMM][++floats]);
            } else {
                rsp += 8;
                sprintf(rbp, "[rbp-%zu]", rsp);

                if (ints > 5)
                    sprintf(next, "    mov rax, qword [rbp+%zu]\n"
                                  "    mov qword %s, rax\n", (ints - 4) * 8, rbp);
                else
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

char *emit_call_arg(AST *ast, char *param_type, char *loc, bool on_stack) {
    char *code;

    switch (ast->type) {
        case AST_INT:
            code = calloc(strlen(loc) + 128, sizeof(char));
            
            if (strcmp(param_type, "float") == 0) {
                if (on_stack)
                    sprintf(code, "    mov eax, %d\n"
                                  "    cvtsi2ss xmm0, eax\n"
                                  "    movss dword %s, xmm0\n", (int)ast->data.digit, loc);
                else
                    sprintf(code, "    mov eax, %d\n"
                                  "    cvtsi2ss %s, eax\n", (int)ast->data.digit, loc);
            } else {
                if (on_stack)
                    sprintf(code, "    mov dword %s, %d\n", loc, (int)ast->data.digit);
                else
                    sprintf(code, "    mov %s, %d\n", loc, (int)ast->data.digit);
            }
            break;
        case AST_FLOAT:
            code = calloc(strlen(loc) + 64, sizeof(char));

            if (on_stack)
                sprintf(code, "    movss xmm0, dword [.f%zu]\n"
                              "    movss dword %s, xmm0\n", float_init(32, ast->data.digit), loc);
            else
                sprintf(code, "    movss %s, dword [.f%zu]\n", loc, float_init(32, ast->data.digit));
            break;
        case AST_VAR: {
            AST *var = sym_find(AST_ASSIGN, ast->scope_def, ast->var.name);
            code = calloc(strlen(loc) + strlen(loc) + 128, sizeof(char));
            
            if (strcmp(param_type, "char") == 0 || strcmp(param_type, "int") == 0) {
                if (strcmp(var->assign.type, "char") == 0) {
                    if (on_stack)
                        sprintf(code, "    movsx eax, byte %s\n"
                                      "    mov dword %s, eax\n", var->assign.rbp, loc);
                    else
                        sprintf(code, "    movsx %s, byte %s\n", loc, var->assign.rbp);
                } else if (strcmp(var->assign.type, "int") == 0) {
                    if (on_stack)
                        sprintf(code, "    mov eax, dword %s\n"
                                      "    mov dword %s, eax\n", var->assign.rbp, loc);
                    else
                        sprintf(code, "    mov %s, dword %s\n", loc, var->assign.rbp);
                } else {
                    sprintf(code, "    cvttss2si %s, dword %s\n", loc, var->assign.rbp);
                }
            } else if (strcmp(param_type, "float") == 0) {
                if (strcmp(var->assign.type, "char") == 0) {
                    if (on_stack)
                        sprintf(code, "    movsx eax, byte %s\n"
                                      "    cvtsi2ss xmm0, eax\n"
                                      "    movss dword %s, xmm0\n", var->assign.rbp, loc);
                    else
                        sprintf(code, "    movsx eax, byte %s\n"
                                      "    cvtsi2ss %s, eax\n", var->assign.rbp, loc);
                } else if (strcmp(var->assign.type, "int") == 0) {
                    if (on_stack)
                        sprintf(code, "    cvtsi2ss xmm0, dword %s\n"
                                      "    movss dword %s, xmm0\n", var->assign.rbp, loc);
                    else
                        sprintf(code, "    cvtsi2ss %s, dword %s\n", loc, var->assign.rbp);
                } else {
                    if (on_stack)
                        sprintf(code, "    movss xmm0, dword %s\n"
                                      "    movss dword %s, xmm0\n", var->assign.rbp, loc);
                    else
                        sprintf(code, "    movss %s, dword %s\n", loc, var->assign.rbp);
                }
            } else {
                if (on_stack)
                    sprintf(code, "    mov rax, qword %s\n"
                                  "    mov qword %s, rax\n", var->assign.rbp, loc);
                else
                    sprintf(code, "    mov %s, qword %s\n", loc, var->assign.rbp);
            }
            break;
        }
        case AST_CALL: {
            code = emit_call(ast);
            char *call_type = sym_find(AST_FUNC, "<global>", ast->call.name)->func.type;
            char *store = calloc(strlen(loc) + 64, sizeof(char));

            if (strcmp(param_type, "char") == 0 || strcmp(param_type, "int") == 0) {
                if (strcmp(call_type, "float") == 0) {
                    if (on_stack)
                        sprintf(store, "    cvttss2si eax, xmm0\n"
                                       "    mov dword %s, eax\n", loc);
                    else
                        sprintf(store, "    cvttss2si %s, xmm0\n", loc);
                } else {
                    if (on_stack)
                        sprintf(store, "    mov dword %s, eax\n", loc);
                    else
                        sprintf(store, "    mov %s, eax\n", loc);
                }
            } else if (strcmp(param_type, "float") == 0) {
                if (strcmp(call_type, "float") == 0) {
                    if (on_stack)
                        sprintf(store, "    movss dword %s, xmm0\n", loc);
                    else
                        sprintf(store, "    movss %s, xmm0\n", loc);
                } else {
                    if (on_stack)
                        sprintf(store, "    cvtsi2ss xmm0, eax\n"
                                       "    movss dword %s, xmm0\n", loc);
                    else
                        sprintf(store, "    cvtsi2ss %s, eax\n", loc);
                }
            } else {
                if (on_stack)
                    sprintf(store, "    mov qword %s, rax\n", loc);
                else
                    sprintf(store, "    mov %s, rax\n", loc);
            }

            code = realloc(code, (strlen(code) + strlen(store) + 1) * sizeof(char));
            strcat(code, store);
            free(store);
            break;
        }
        case AST_MATH: {
            code = emit_ast(ast);
            char *store = calloc(strlen(loc) + 128, sizeof(char));

            if (strcmp(param_type, "char") == 0 || strcmp(param_type, "int") == 0) {
                if (float_math_result) {
                    if (on_stack)
                        sprintf(store, "    cvttss2si eax, xmm0\n"
                                       "    mov dword %s, eax\n", loc);
                    else
                        sprintf(store, "    cvttss2si %s, xmm0\n", loc);
                } else {
                    if (on_stack)
                        sprintf(store, "    mov dword %s, eax\n", loc);
                    else
                        sprintf(store, "    mov %s, eax\n", loc);
                }
            } else {
                if (float_math_result) {
                    if (on_stack)
                        sprintf(store, "    movss dword %s, xmm0\n", loc);
                    else
                        sprintf(store, "    movss %s, xmm0\n", loc);
                } else {
                    if (on_stack)
                        sprintf(store, "    cvtsi2ss xmm0, eax\n"
                                       "    movss dword %s, xmm0\n", loc);
                    else
                        sprintf(store, "    cvtsi2ss %s, eax\n", loc);
                }
            }

            code = realloc(code, (strlen(code) + strlen(store) + 1) * sizeof(char));
            strcat(code, store);
            free(store);
            break;
        }
        default: assert(false);
    }

    return code;
}

char *emit_math(AST *ast);

char *emit_call(AST *ast) {
    char *code = calloc(1, sizeof(char));
    char *next;
    char *loc;
    AST *sym = sym_find(AST_FUNC, "<global>", ast->call.name);
    AST *param;
    AST *arg;
    size_t ints = 0;
    size_t floats = 0;
    size_t beg_rsp = rsp;

    for (size_t i = 0; i < sym->func.params_cnt; i++) {
        param = sym->func.params[i];
        arg = ast->call.args[i];
        loc = calloc(64, sizeof(char));

        if (strcmp(param->assign.type, "char") == 0 || strcmp(param->assign.type, "int") == 0) {
            if (ints < 6)
                strcpy(loc, regs[int_params[ints++]][DWORD]);
            else {
                rsp += 8;
                sprintf(loc, "[rbp-%zu]", rsp);
            }
        } else if (strcmp(param->assign.type, "float") == 0)
            if (floats < 15)
                strcpy(loc, regs[XMM][++floats]);
            else {
                rsp += 8;
                sprintf(loc, "[rbp-%zu]", rsp);
            }
        else {
            if (ints < 6)
                strcpy(loc, regs[int_params[ints++]][QWORD]);
            else {
                rsp += 8;
                sprintf(loc, "[rbp-%zu]", rsp);
            }
        }

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

            next = emit_call_arg(arg, param->assign.type, loc, strstr(loc, "[rbp-") != NULL ? true : false);

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
        
        next = emit_call_arg(arg, param->assign.type, loc, strstr(loc, "[rbp-") != NULL ? true : false);
        code = realloc(code, (strlen(code) + strlen(next) + 1) * sizeof(char));
        strcat(code, next);
        free(next);
        free(loc);
    }
    code = realloc(code, (strlen(code) + strlen(ast->call.name) + 32) * sizeof(char));
    strcat(code, "    call ");
    strcat(code, ast->call.name);
    strcat(code, "_\n");

    if (rsp != beg_rsp) {
        char *temp = calloc(strlen(code) + 128, sizeof(char));
        sprintf(temp, "    sub rsp, %zu\n"
                      "%s"
                      "    add rsp, %zu\n", rsp - beg_rsp, code, rsp - beg_rsp);
        free(code);
        code = temp;
        rsp = beg_rsp;
    }

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
        case AST_MATH: {
            code = emit_ast(value);
            char *fix = calloc(strlen(rbp) + 128, sizeof(char));

            if (strcmp(type, "char") == 0) {
                if (float_math_result)
                    sprintf(fix, "    cvttss2si eax, xmm0\n"
                                 "    mov byte %s, al\n", rbp);
                else
                    sprintf(fix, "    mov byte %s, al\n", rbp);
            } else if (strcmp(type, "int") == 0) {
                if (float_math_result)
                    sprintf(fix, "    cvttss2si eax, xmm0\n"
                                 "    mov dword %s, eax\n", rbp);
                else
                    sprintf(fix, "    mov dword %s, eax\n", rbp);
            } else {
                if (float_math_result)
                    sprintf(fix, "    movss dword %s, xmm0\n", rbp);
                else
                    sprintf(fix, "    cvtsi2ss xmm0, eax\n"
                                 "    movss dword %s, xmm0\n", rbp);
            }

            code = realloc(code, (strlen(code) + strlen(fix) + 1) * sizeof(char));
            strcat(code, fix);
            free(fix);
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
                    sprintf(code, "    mov eax, dword %s\n", var->assign.rbp);
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
        case AST_MATH: {
            code = emit_ast(value);

            if (strcmp(type, "float") == 0 && !float_math_result) {
                code = realloc(code, (strlen(code) + 32) * sizeof(char));
                strcat(code, "    cvtsi2ss xmm0, eax\n");
            } else if (strcmp(type, "float") != 0 && float_math_result) {
                code = realloc(code, (strlen(code) + 32) * sizeof(char));
                strcat(code, "    cvttss2si eax, xmm0\n");
            }
            break;
        }
        default: assert(false);
    }

    code = realloc(code, (strlen(code) + strlen(ret) + 1) * sizeof(char));
    strcat(code, ret);
    return code;
}

char *emit_math_expr(AST *left, AST *right, TokType type) {
    char *code;
    char left_value[64];
    char right_value[64];
    char *setup = NULL;
    bool is_float = false;

    switch (left->type) {
        case AST_INT:
            code = calloc(64, sizeof(char));
            sprintf(code, "    mov eax, %d\n", (int)left->data.digit);
            break;
        case AST_FLOAT:
            code = calloc(64, sizeof(char));
            sprintf(code, "    movss xmm0, dword [.f%zu]\n", float_init(32, left->data.digit));
            is_float = true;
            break;
        case AST_VAR: {
            AST *var = sym_find(AST_ASSIGN, left->scope_def, left->var.name);
            code = calloc(strlen(var->assign.rbp) + 64, sizeof(char));

            if (strcmp(var->assign.type, "char") == 0)
                sprintf(code, "    movsx eax, byte %s\n", var->assign.rbp);
            else if (strcmp(var->assign.type, "int") == 0)
                sprintf(code, "    mov eax, dword %s\n", var->assign.rbp);
            else {
                sprintf(code, "    movss xmm0, dword %s\n", var->assign.rbp);
                is_float = true;
            }
            break;
        }
        case AST_CALL: {
            code = emit_ast(left);
            AST *sym = sym_find(AST_FUNC, "<global>", left->call.name);

            if (strcmp(sym->func.type, "float") == 0)
                is_float = true;
            break;
        }
        case AST_MATH_VAR: {
            is_float = left->math_var.is_float;

            if (left->math_var.stack_rbp != NULL) {
                code = calloc(strlen(left->math_var.stack_rbp) + 64, sizeof(char));

                if (left->math_var.is_float)
                    sprintf(code, "    movss xmm0, dword %s\n", left->math_var.stack_rbp);
                else
                    sprintf(code, "    mov eax, dword %s\n", left->math_var.stack_rbp);
                break;
            }

            code = calloc(1, sizeof(char));
            break;
        }
        default: assert(false);
    }

    if (is_float)
        strcpy(left_value, "xmm0");
    else
        strcpy(left_value, "eax");

    switch (right->type) {
        case AST_INT:
            if (is_float) {
                setup = calloc(64, sizeof(char));
                sprintf(setup, "    mov eax, %d\n"
                               "    cvtsi2ss xmm1, eax\n", (int)right->data.digit);
                strcpy(right_value, "xmm1");
            } else
                sprintf(right_value, "%d", (int)right->data.digit);
            break;
        case AST_FLOAT:
            if (!is_float) {
                setup = calloc(32, sizeof(char));
                strcpy(setup, "    cvtsi2ss xmm0, eax\n");
                strcpy(left_value, "xmm0");
                is_float = true;
            }

            sprintf(right_value, "dword [.f%zu]", float_init(32, right->data.digit));
            break;
        case AST_VAR: {
            AST *var = sym_find(AST_ASSIGN, right->scope_def, right->var.name);

            if (is_float) {
                if (strcmp(var->assign.type, "char") == 0) {
                    setup = calloc(strlen(var->assign.rbp) + 64, sizeof(char));
                    sprintf(setup, "    movsx eax, byte %s\n"
                                   "    cvtsi2ss xmm1, eax\n", var->assign.rbp);
                    strcpy(right_value, "xmm1");
                } else if (strcmp(var->assign.type, "int") == 0) {
                    setup = calloc(strlen(var->assign.rbp) + 64, sizeof(char));
                    sprintf(setup, "    cvtsi2ss xmm1, dword %s\n", var->assign.rbp);
                    strcpy(right_value, "xmm1");
                } else
                    sprintf(right_value, "dword %s", var->assign.rbp);
            } else {
                if (strcmp(var->assign.type, "char") == 0) {
                    setup = calloc(strlen(var->assign.rbp) + 64, sizeof(char));
                    sprintf(setup, "    movsx ebx, byte %s\n", var->assign.rbp);
                    strcpy(right_value, "ebx");
                } else if (strcmp(var->assign.type, "int") == 0)
                    sprintf(right_value, "dword %s", var->assign.rbp);
                else {
                    setup = calloc(32, sizeof(char));
                    sprintf(setup, "    cvtsi2ss xmm0, eax\n");
                    strcpy(left_value, "xmm0");
                    sprintf(right_value, "dword %s", var->assign.rbp);
                    is_float = true;
                }
            }
            break;
        }
        case AST_CALL: {
            AST *sym = sym_find(AST_FUNC, "<global>", right->call.name);
            rsp += 8;
            setup = emit_ast(right);
            char *save;

            // Please forgive me for this awful looking code
            if (is_float) {
                if (rsp + 8 > rsp_cap) {
                    rsp_cap += 8;
                    save = calloc(strlen(setup) + 128, sizeof(char));
                    rsp_cap -= 8;

                    if (strcmp(sym->func.type, "float") == 0)
                        sprintf(save, "    sub rsp, 8\n"
                                      "    movss dword [rbp-%zu], xmm0\n"
                                      "%s"
                                      "    movss xmm1, xmm0\n"
                                      "    movss xmm0, dword [rbp-%zu]\n"
                                      "    add rsp, 8\n", rsp + 8, setup, rsp + 8);
                    else
                        sprintf(save, "    sub rsp, 8\n"
                                      "    movss dword [rbp-%zu], xmm0\n"
                                      "%s"
                                      "    movss xmm0, dword [rbp-%zu]\n"
                                      "    cvtsi2ss xmm1, eax\n"
                                      "    add rsp, 8\n", rsp + 8, setup, rsp + 8);
                } else {
                    save = calloc(strlen(setup) + 128, sizeof(char));

                    if (strcmp(sym->func.type, "float") == 0)
                        sprintf(save, "    movss dword [rbp-%zu], xmm0\n"
                                      "%s"
                                      "    movss xmm1, xmm0\n"
                                      "    movss xmm0, dword [rbp-%zu]\n", rsp + 8, setup, rsp + 8);
                    else
                        sprintf(save, "    movss dword [rbp-%zu], xmm0\n"
                                      "%s"
                                      "    movss xmm0, dword [rbp-%zu]\n"
                                      "    cvtsi2ss xmm1, eax\n", rsp + 8, setup, rsp + 8);
                }

                strcpy(left_value, "xmm0");
                strcpy(right_value, "xmm1");
            } else {
                rsp_cap += 8;
                save = calloc(strlen(setup) + 128, sizeof(char));
                rsp_cap -= 8;

                if (strcmp(sym->func.type, "float") == 0) {
                    sprintf(save, "    push rax\n"
                                  "%s"
                                  "    movss xmm1, xmm0\n"
                                  "    pop rax\n"
                                  "    cvtsi2ss xmm0, eax\n", setup);

                    strcpy(left_value, "xmm0");
                    strcpy(right_value, "xmm1");
                    is_float = true;
                } else {
                    sprintf(save, "    push rax\n"
                                  "%s"
                                  "    mov ebx, eax\n"
                                  "    pop rax\n", setup);
                    strcpy(right_value, "ebx");
                }
            }

            rsp -= 8;
            free(setup);
            setup = save;
            break;
        }
        case AST_MATH_VAR: {
            if (right->math_var.stack_rbp != NULL) {
                if (is_float) {
                    if (right->math_var.is_float)
                        sprintf(right_value, "dword %s", right->math_var.stack_rbp);
                    else {
                        setup = calloc(strlen(right->math_var.stack_rbp) + 64, sizeof(char));
                        sprintf(setup, "    cvtsi2ss xmm1, dword %s\n", right->math_var.stack_rbp);
                        strcpy(right_value, "xmm1");
                    }
                } else {
                    if (right->math_var.is_float) {
                        setup = calloc(64, sizeof(char));
                        strcpy(setup, "    cvtsi2ss xmm0, eax\n");
                        strcpy(left_value, "xmm0");
                        is_float = true;
                    }
                    
                    sprintf(right_value, "dword %s", right->math_var.stack_rbp);
                }
                break;
            }

            // It's ensured that xmm0 or eax won't have been corrupted by, for example, a call by left,
            // it is only in the reg if it will not be corrupted
            char *temp = calloc(strlen(code) + 64, sizeof(char));

            if (is_float) {
                if (right->math_var.is_float)
                    strcpy(temp, "    movss xmm1, xmm0\n");
                else
                    sprintf(temp, "    cvtsi2ss xmm1, eax\n");
                
                strcpy(right_value, "xmm1");
            } else {
                if (right->math_var.is_float) {
                    strcpy(temp, "    movss xmm1, xmm0\n");
                    code = realloc(code, (strlen(code) + 32) * sizeof(char));
                    strcat(code, "    cvtsi2ss xmm0, eax\n");

                    strcpy(left_value, "xmm0");
                    strcpy(right_value, "xmm1");
                } else {
                    sprintf(temp, "    mov ebx, eax\n");
                    strcpy(right_value, "ebx");
                }
            }

            strcat(temp, code);
            free(code);
            code = temp;
            break;
        }
        default:  printf(">>>%s\n", ast_types[right->type]); assert(false);
    }

    if (setup != NULL) {
        code = realloc(code, (strlen(code) + strlen(setup) + 1) * sizeof(char));
        strcat(code, setup);
        free(setup);
    }

    char *math = calloc(strlen(left_value) + strlen(right_value) + 256, sizeof(char));

    switch (type) {
        case TOK_PLUS:
            if (is_float)
                sprintf(math, "    addss %s, %s\n", left_value, right_value);
            else
                sprintf(math, "    add %s, %s\n", left_value, right_value);
            break;
        case TOK_MINUS:
            if (is_float)
                sprintf(math, "    subss %s, %s\n", left_value, right_value);
            else
                sprintf(math, "    sub %s, %s\n", left_value, right_value);
            break;
        case TOK_STAR:
            if (is_float)
                sprintf(math, "    mulss %s, %s\n", left_value, right_value);
            else
                sprintf(math, "    imul %s, %s\n", left_value, right_value);
            break;
        case TOK_SLASH:
            if (is_float) {
                sprintf(math, "    divss %s, %s\n", left_value, right_value);
                break;
            }

            if (right->data.digit >= 0 && is_power_of_two((unsigned int)right->data.digit))
                sprintf(math, "    sar %s, %u\n", left_value, power_of_two((unsigned int)right->data.digit));
            else
                sprintf(math, "    cqo\n"
                              "    idiv %s\n", right_value);
            break;
        default:
            if (right->data.digit >= 0 && is_power_of_two((unsigned int)right->data.digit))
                sprintf(math, "    and %s, %u\n", left_value, (unsigned int)right->data.digit - 1);
            else
                sprintf(math, "    xor rdi, rdi\n"
                              "    idiv %s\n"
                              "    mov eax, edx\n", right_value);
            break;
    }

    code = realloc(code, (strlen(code) + strlen(math) + 1) * sizeof(char));
    strcat(code, math);
    free(math);

    float_math_result = is_float;
    return code;
}

char *emit_math(AST *ast) {
    char *code = calloc(1, sizeof(char));
    char *next;

    AST **expr = ast->math.expr;
    size_t expr_cnt = ast->math.expr_cnt;

    bool is_float = false;
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
    AST *oper;
    AST *next_left;
    size_t oper_pos;
    size_t next_oper_pos;
    int j;
    
    size_t beg_rsp = rsp;
    size_t beg_cap = rsp_cap;

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

        next = emit_math_expr(left, right, oper->oper.kind);
        code = realloc(code, (strlen(code) + strlen(next) + 1) * sizeof(char));
        strcat(code, next);
        free(next);

        if (!is_float)
            is_float = float_math_result;

        right->active = false;

        if (i == oper_cnt - 1)
            break;

        ast_fields_del(left);
        left->type = AST_MATH_VAR;
        left->math_var.is_float = is_float;
        next_oper_pos = order[i + 1];

        if (abs((int)oper_pos - (int)next_oper_pos) > 2 && i != oper_cnt - 2) {
save_math_result:
            // Not used in the next expression, needs to be saved to the stack
            char *save = calloc(128, sizeof(char));
            rsp += 8;
            rsp_cap += 8;

            if (is_float)
                sprintf(save, "    movss dword [rbp-%zu], xmm0\n", rsp);
            else
                sprintf(save, "    mov dword [rbp-%zu], eax\n", rsp);

            code = realloc(code, (strlen(code) + strlen(save) + 1) * sizeof(char));
            strcat(code, save);
            free(save);

            left->math_var.stack_rbp = calloc(64, sizeof(char));
            sprintf(left->math_var.stack_rbp, "[rbp-%zu]", rsp);
            break;
        }

        next_left = expr[order[i + 1] - 1];
        while (!next_left->active)
            next_left = expr[order[i + 1] + --j];

        if (next_left->type == AST_CALL) // Will corrupt the reg so must be saved
            goto save_math_result;

        // Used in the next expression, no need to save to the stack
        left->math_var.reg_name = is_float ? "xmm0" : "eax";
        left->math_var.stack_rbp = NULL;
    }

    rsp = beg_rsp;
    rsp_cap = beg_cap;

    float_math_result = is_float;
    return code;
}

char *emit_ast(AST *ast) {
    switch (ast->type) {
        case AST_ROOT: return emit_root(ast);
        case AST_FUNC: return emit_func(ast);
        case AST_CALL: return emit_call(ast);
        case AST_ASSIGN: return emit_assign(ast);
        case AST_RET: return emit_ret(ast);
        case AST_MATH: return emit_math(ast);
        default:
            fprintf(stderr, "pcc: Error: Missing backend for '%s'.\n", ast_types[ast->type]);
            exit(EXIT_FAILURE);
    }
}