#ifndef MINI_TEST_H
#define MINI_TEST_H

/* 零依赖断言框架。RUN_ISOLATED 用子进程运行可能崩溃的用例，
 * 将段错误记录为 FAIL，避免整套测试被中断。 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#define CHILD_RC_FILE "/tmp/mt_child_rc"
#define CHILD_KV_FILE "/tmp/mt_child_kv"

static int g_pass = 0;
static int g_fail = 0;

#define CASE(name) printf("\n[CASE] %s\n", (name))

#define CHECK(cond) do {                                                      \
        if (cond) { g_pass++; printf("  PASS  %s:%d  %s\n",                    \
                                     __FILE__, __LINE__, #cond); }             \
        else      { g_fail++; printf("  FAIL  %s:%d  %s\n",                    \
                                     __FILE__, __LINE__, #cond); }             \
    } while (0)

#define CHECK_EQ_INT(a, b) do {                                               \
        long _a = (long)(a), _b = (long)(b);                                  \
        if (_a == _b) { g_pass++; printf("  PASS  %s:%d  %s == %s (%ld)\n",    \
                                         __FILE__, __LINE__, #a, #b, _a); }    \
        else { g_fail++; printf("  FAIL  %s:%d  %s == %s (got %ld, want %ld)\n",\
                                __FILE__, __LINE__, #a, #b, _a, _b); }         \
    } while (0)

#define CHECK_EQ_STR(a, b) do {                                               \
        const char *_a = (a), *_b = (b);                                      \
        if (_a && _b && strcmp(_a, _b) == 0) {                                \
            g_pass++; printf("  PASS  %s:%d  \"%s\" == \"%s\"\n",             \
                             __FILE__, __LINE__, _a, _b); }                    \
        else { g_fail++; printf("  FAIL  %s:%d  (got \"%s\", want \"%s\")\n",  \
                                __FILE__, __LINE__,                            \
                                _a ? _a : "(null)", _b ? _b : "(null)"); }     \
    } while (0)

#define NOTE(...) do { printf("  NOTE  "); printf(__VA_ARGS__); printf("\n"); } while (0)

#define RUN_ISOLATED(label, fn) do {                                          \
        printf("  ---- 隔离执行: %s\n", (label));                              \
        fflush(stdout);                                                       \
        unlink(CHILD_RC_FILE); unlink(CHILD_KV_FILE);                         \
        pid_t _p = fork();                                                    \
        if (_p < 0) { g_fail++; printf("  FAIL  fork 失败\n"); }               \
        else if (_p == 0) {                                                   \
            int _rc = fn();                                                   \
            FILE *_f = fopen(CHILD_RC_FILE, "w");                             \
            if (_f) { fprintf(_f, "%d", _rc); fclose(_f); }                   \
            fflush(stdout);                                                   \
            _exit(0);                                                         \
        } else {                                                              \
            int _st = 0;                                                      \
            (void)waitpid(_p, &_st, 0);                                       \
            if (WIFSIGNALED(_st)) {                                           \
                g_fail++;                                                     \
                printf("  FAIL  %s : 子进程被信号终止 signal=%d%s\n", (label), \
                       WTERMSIG(_st),                                         \
                       WTERMSIG(_st) == 11 ? " (SIGSEGV 段错误)" : "");       \
            } else {                                                          \
                int _rc = -999;                                               \
                FILE *_f = fopen(CHILD_RC_FILE, "r");                         \
                if (_f) { fscanf(_f, "%d", &_rc); fclose(_f); }               \
                if (_rc == 0) { g_pass++; printf("  PASS  %s (rc=0)\n", (label)); } \
                else { g_fail++; printf("  FAIL  %s (rc=%d)\n", (label), _rc); } \
            }                                                                 \
        }                                                                     \
    } while (0)

#define SUMMARY() do {                                                        \
        unlink(CHILD_RC_FILE);                                                \
        unlink(CHILD_KV_FILE);                                                \
        printf("\n================== SUMMARY ==================\n");           \
        printf("  PASS: %d    FAIL: %d    TOTAL: %d\n",                       \
               g_pass, g_fail, g_pass + g_fail);                              \
        printf("  FAIL 中带 SIGSEGV/VULN 的即为缺陷，见缺陷清单\n");            \
        printf("=============================================\n");             \
        return g_fail ? 1 : 0;                                                \
    } while (0)

#endif
