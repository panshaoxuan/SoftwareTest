/*
 * AI 语义检索模块（模块二）C 单元测试
 *
 * 被测对象：src_cgi/ai_cgi.cpp
 *
 * 为什么直接 #include 被测源码：
 *   handle_describe / handle_search / handle_rebuild 都是 static，测试无法 extern 调用；
 *   而 main() 的函数体被 FCGI_Accept() 循环包住，非 FCGI 进程下无法驱动。
 *   因此把源码纳入本 TU 以访问 static 函数，并把其 main 改名为 ai_cgi_main 避免符号冲突。
 *
 * 为什么被测源码放在文件**末尾**：
 *   ai_cgi.cpp 包含 fcgi_stdio.h，会宏替换 printf / FILE / stdin / stdout 等 52 个符号。
 *   本文件先写测试代码、最后纳入源码，测试侧 stdio 才保持真实语义，输出才能正常打印。
 *   唯一放在末尾的是 bind_fcgi_streams()，因为它需要 fcgi_stdio.h 声明的 _fcgi_sF[]。
 *
 * 外部依赖全部打桩（不链接 mysqlclient / hiredis / faiss 的真实实现）：
 *   verify_token（Redis token 校验）、msql_conn / process_result_one（MySQL 查询）、
 *   mysql_* 家族、dashscope_describe_image / dashscope_get_embedding（模型调用）、
 *   faiss_* 家族。打桩状态由 g_* 变量控制，便于构造各分支。
 *
 * 边界：cmd 分发（缺 cmd / 未知 cmd / 无 POST body）位于 FCGI 主循环内，单元测试不可达；
 *       该三条用例以接口级 curl 测试覆盖，见 .course2026/evidence/01_cases.txt。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

extern "C" {
#include <mysql/mysql.h>
#include "util_cgi.h"
#include "deal_mysql.h"
#include "faiss_wrapper.h"
#include "dashscope_api.h"
}

#include "mini_test.h"

/* ==================================================================== */
/* 被测函数前置声明（定义在文件末尾纳入的 ai_cgi.cpp 中，均为 static）      */
/* ==================================================================== */

static int handle_describe(char *post_data);
static int handle_search(char *post_data);
static int handle_rebuild(char *post_data);

/* ==================================================================== */
/* 打桩状态                                                              */
/* ==================================================================== */

static int  g_token_ret = 0;              /* verify_token 返回值，0=通过 */
static int  g_owns_file = 1;              /* 是否拥有该文件 */
static int  g_ai_status = -1;             /* 已有 AI 记录状态，-1=无记录 */
static int  g_describe_ret = 0;           /* dashscope_describe_image 返回值 */
static char g_describe_out[512] = {0};    /* 模型返回的描述 */
static long g_faiss_ntotal = 0;           /* faiss_get_ntotal */
static int  g_faiss_search_ret = 0;       /* faiss_search 返回条数 */
static char g_all_sql[16384] = {0};       /* 累积记录全部 mysql_query，供断言落库内容 */

static void reset_stub_state(void)
{
    g_token_ret = 0;
    g_owns_file = 1;
    g_ai_status = -1;
    g_describe_ret = 0;
    g_faiss_ntotal = 0;
    g_faiss_search_ret = 0;
    memset(g_all_sql, 0, sizeof(g_all_sql));
    snprintf(g_describe_out, sizeof(g_describe_out), "桩描述：一只动物在雪地中。");
}

/* ==================================================================== */
/* 依赖桩实现                                                            */
/* ==================================================================== */

extern "C" {

int verify_token(char *user, char *token)
{
    (void)user; (void)token;
    return g_token_ret;
}

MYSQL *msql_conn(char *user_name, char *passwd, char *db_name)
{
    (void)user_name; (void)passwd; (void)db_name;
    return (MYSQL *)0x1;   /* 非空哑指针；真正的 MySQL 调用已在下面全部打桩 */
}

/*
 * process_result_one 按 SQL 子串路由，模拟四类查询：
 *   user_file_list    -> 归属校验，返回文件名
 *   user_file_ai_desc -> 已有 AI 记录状态
 *   file_info         -> 文件实体 URL（图片分支据此拼下载地址）
 *   其他              -> 失败
 */
int process_result_one(MYSQL *conn, char *sql_cmd, char *buf)
{
    (void)conn;
    if (!sql_cmd || !buf) return -1;

    if (strstr(sql_cmd, "user_file_list")) {
        if (!g_owns_file) return -1;
        strcpy(buf, "fixture.jpg");
        return 0;
    }
    if (strstr(sql_cmd, "user_file_ai_desc")) {
        if (g_ai_status < 0) return -1;
        sprintf(buf, "%d", g_ai_status);
        return 0;
    }
    if (strstr(sql_cmd, "file_info")) {
        strcpy(buf, "http://172.30.0.3:80/group1/M00/00/00/fixture.jpg");
        return 0;
    }
    return -1;
}

void print_error(MYSQL *conn, const char *title) { (void)conn; (void)title; }

/* ---- mysql_* 家族：全部桩掉，避免链接 libmysqlclient ---- */

int mysql_query(MYSQL *mysql, const char *q)
{
    (void)mysql;
    /* 累积记录：describe/search/rebuild 会连发多条 SQL，只看最后一条会漏判落库内容 */
    if (q) {
        size_t used = strlen(g_all_sql);
        if (used < sizeof(g_all_sql) - 2) {
            snprintf(g_all_sql + used, sizeof(g_all_sql) - used, "%s\n", q);
        }
    }
    return 0;
}

const char *mysql_error(MYSQL *mysql) { (void)mysql; return ""; }
unsigned int mysql_errno(MYSQL *mysql) { (void)mysql; return 0; }
void mysql_close(MYSQL *mysql) { (void)mysql; }

MYSQL_RES *mysql_store_result(MYSQL *mysql) { (void)mysql; return (MYSQL_RES *)0x1; }
MYSQL_ROW mysql_fetch_row(MYSQL_RES *r) { (void)r; return NULL; }
unsigned long *mysql_fetch_lengths(MYSQL_RES *r) { (void)r; return NULL; }
my_ulonglong mysql_num_rows(MYSQL_RES *r) { (void)r; return 0; }
unsigned int mysql_num_fields(MYSQL_RES *r) { (void)r; return 0; }
void mysql_free_result(MYSQL_RES *r) { (void)r; }
my_ulonglong mysql_affected_rows(MYSQL *mysql) { (void)mysql; return 1; }

unsigned long mysql_real_escape_string(MYSQL *mysql, char *to,
                                       const char *from, unsigned long length)
{
    (void)mysql;
    if (length) memcpy(to, from, length);
    to[length] = '\0';
    return length;
}

/* ---- 模型调用桩 ---- */

int dashscope_describe_image(const char *api_key, const char *image_url,
                             char *out_desc, int max_len)
{
    (void)api_key; (void)image_url;
    if (g_describe_ret != 0) return g_describe_ret;
    snprintf(out_desc, max_len, "%s", g_describe_out);
    return 0;
}

int dashscope_get_embedding(const char *api_key, const char *model,
                            const char *text, float *out_vector, int dimension)
{
    (void)api_key; (void)model; (void)text;
    for (int i = 0; i < dimension; i++) out_vector[i] = 0.01f;
    return 0;
}

/* ---- FAISS 桩 ---- */

int  faiss_init(const char *p, int d) { (void)p; (void)d; return 0; }
int  faiss_add(float *v, int d) { (void)v; (void)d; return 1; }
int  faiss_save(const char *p) { (void)p; return 0; }
void faiss_set_auto_save(int e) { (void)e; }
long faiss_get_ntotal(void) { return g_faiss_ntotal; }
void faiss_reset(void) {}

int faiss_search(float *query, int dimension, int topk,
                 long *out_ids, float *out_scores)
{
    (void)query; (void)dimension;
    if (g_faiss_search_ret <= 0) return g_faiss_search_ret;
    for (int i = 0; i < g_faiss_search_ret && i < topk; i++) {
        out_ids[i] = i + 1;
        out_scores[i] = 0.90f - 0.01f * i;
    }
    return g_faiss_search_ret;
}

/*
 * vector_l2_normalize 是纯数学函数，按真实算法实现（不简化），
 * 使被测代码计算余弦相似度所依赖的归一化语义与生产一致。
 */
void vector_l2_normalize(float *vector, int dimension)
{
    float sum = 0.0f;
    if (!vector || dimension <= 0) return;
    for (int i = 0; i < dimension; i++) sum += vector[i] * vector[i];
    if (sum <= 0.0f) return;
    {
        float norm = sqrtf(sum);
        for (int i = 0; i < dimension; i++) vector[i] /= norm;
    }
}

/*
 * query_parse_key_value 在 util_cgi.c 中与 verify_token 同 TU，
 * 直接链接会带进真实的 Redis token 校验而与上面的桩冲突，
 * 故按原语义在此实现：从查询串中取 key= 后的值，遇 & 或串尾结束。
 */
int query_parse_key_value(const char *query, const char *key,
                          char *value, int *value_len_p)
{
    const char *p;
    size_t key_len;

    if (!query || !key || !value) return -1;
    key_len = strlen(key);
    value[0] = '\0';
    if (value_len_p) *value_len_p = 0;

    p = query;
    while ((p = strstr(p, key)) != NULL) {
        /* 必须是串首或紧跟 & 才算一个键，避免匹配到子串 */
        if (p != query && *(p - 1) != '&') { p += key_len; continue; }
        p += key_len;
        if (*p != '=') continue;
        p++;
        {
            const char *end = strchr(p, '&');
            size_t n = end ? (size_t)(end - p) : strlen(p);
            memcpy(value, p, n);
            value[n] = '\0';
            if (value_len_p) *value_len_p = (int)n;
            return 0;
        }
    }
    return -1;
}

} /* extern "C" */

/* ==================================================================== */
/* 响应捕获                                                              */
/*                                                                       */
/* 被测代码用 FCGI_printf 输出，目标是 _fcgi_sF[1].stdio_stream。         */
/* 这里先把真实的 stdin/stdout 存下来（此刻尚未包含 fcgi_stdio.h，        */
/* 故 stdin/stdout 是真符号），再经 dup2 把 fd 1 重定向到文件抓响应。      */
/* ==================================================================== */

#define CAPTURE_FILE "/tmp/ai_test_resp.txt"

static FILE *g_real_stdin = NULL;
static FILE *g_real_stdout = NULL;

static void grab_real_streams(void)
{
    g_real_stdin = stdin;
    g_real_stdout = stdout;
}

/* 以下两个函数定义在文件末尾（包含 ai_cgi.cpp 之后），此处仅前置声明：
 *   bind_fcgi_streams 需要 fcgi_stdio.h 声明的 _fcgi_sF[]；
 *   setup_test_paths  需要 ai_cgi.cpp 内的 static 全局路径变量。 */
static void bind_fcgi_streams(void);
static void setup_test_paths(void);

/* 执行一次被测处理函数并返回其打印的响应体 */
static int run_handler(int (*fn)(char *), const char *json,
                       char *out, int out_len)
{
    int saved = dup(STDOUT_FILENO);
    int fd = open(CAPTURE_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    int rc, rfd, n = 0;

    if (fd < 0 || saved < 0) return -1;
    fflush(stdout);
    dup2(fd, STDOUT_FILENO);
    close(fd);

    rc = fn((char *)json);

    fflush(stdout);
    dup2(saved, STDOUT_FILENO);
    close(saved);

    rfd = open(CAPTURE_FILE, O_RDONLY);
    if (rfd >= 0) {
        n = read(rfd, out, out_len - 1);
        if (n < 0) n = 0;
        out[n] = '\0';
        close(rfd);
    } else {
        out[0] = '\0';
    }
    return rc;
}

static void setup_once(void)
{
    grab_real_streams();
    bind_fcgi_streams();
    setup_test_paths();
}

/* ==================================================================== */
/* 用例                                                                  */
/* ==================================================================== */

/* AIC-UT-006：非法 JSON */
static int case_invalid_json(void)
{
    char resp[1024] = {0};
    reset_stub_state();
    int _fail0 = g_fail;
    CASE("AIC-UT-006 describe 非法 JSON");
    run_handler(handle_describe, "{not-json", resp, sizeof(resp));
    CHECK(strstr(resp, "\"msg\":\"invalid json\"") != NULL);
    return (g_fail > _fail0) ? 1 : 0;
}

/* AIC-UT-005：缺必填字段 */
static int case_missing_fields(void)
{
    char resp[1024] = {0};
    reset_stub_state();
    int _fail0 = g_fail;
    CASE("AIC-UT-005 describe 缺字段(无 md5)");
    run_handler(handle_describe, "{\"user\":\"u\",\"token\":\"t\"}", resp, sizeof(resp));
    CHECK(strstr(resp, "\"msg\":\"missing fields\"") != NULL);
    return (g_fail > _fail0) ? 1 : 0;
}

/* AIC-UT-003：缺 api_key */
static int case_missing_api_key_describe(void)
{
    char resp[1024] = {0};
    reset_stub_state();
    int _fail0 = g_fail;
    CASE("AIC-UT-003 describe 缺 api_key");
    run_handler(handle_describe,
                "{\"user\":\"u\",\"token\":\"t\",\"md5\":\"m\",\"filename\":\"f.jpg\"}",
                resp, sizeof(resp));
    CHECK(strstr(resp, "\"msg\":\"missing api_key\"") != NULL);
    return (g_fail > _fail0) ? 1 : 0;
}

/* AIC-UT-004：token 非法 -> code 4 */
static int case_bad_token_describe(void)
{
    char resp[1024] = {0};
    reset_stub_state();
    int _fail0 = g_fail;
    g_token_ret = -1;
    CASE("AIC-UT-004 describe 非法 token -> code 4");
    run_handler(handle_describe,
                "{\"user\":\"u\",\"token\":\"bad\",\"md5\":\"m\",\"filename\":\"f.jpg\",\"api_key\":\"k\"}",
                resp, sizeof(resp));
    CHECK(strstr(resp, "\"code\":4") != NULL);
    CHECK(strstr(resp, "\"msg\":\"token error\"") != NULL);
    return (g_fail > _fail0) ? 1 : 0;
}

/* AIC-UT-007：文件不存在或无权 -> 越权校验必须生效 */
static int case_file_not_found(void)
{
    char resp[1024] = {0};
    reset_stub_state();
    int _fail0 = g_fail;
    g_owns_file = 0;
    CASE("AIC-UT-007 describe 越权/不存在 md5");
    run_handler(handle_describe,
                "{\"user\":\"u\",\"token\":\"t\",\"md5\":\"deadbeef\",\"filename\":\"f.jpg\",\"api_key\":\"k\"}",
                resp, sizeof(resp));
    CHECK(strstr(resp, "\"msg\":\"file not found or no permission\"") != NULL);
    return (g_fail > _fail0) ? 1 : 0;
}

/* AIC-UT-008：已有记录且未带 force -> already exists */
static int case_already_exists(void)
{
    char resp[1024] = {0};
    reset_stub_state();
    int _fail0 = g_fail;
    g_ai_status = 1;
    CASE("AIC-UT-008 describe 重复调用 -> already exists");
    run_handler(handle_describe,
                "{\"user\":\"u\",\"token\":\"t\",\"md5\":\"m\",\"filename\":\"f.jpg\",\"api_key\":\"k\"}",
                resp, sizeof(resp));
    CHECK(strstr(resp, "\"code\":0") != NULL);
    CHECK(strstr(resp, "\"msg\":\"already exists\"") != NULL);
    return (g_fail > _fail0) ? 1 : 0;
}

/* AIC-UT-001（单元层）：模型正常返回时，描述应取自模型并落库 */
static int case_describe_ok(void)
{
    char resp[1024] = {0};
    reset_stub_state();
    int _fail0 = g_fail;
    CASE("AIC-UT-001(单元层) describe 正常路径：描述取自模型并落库");
    run_handler(handle_describe,
                "{\"user\":\"u\",\"token\":\"t\",\"md5\":\"m\",\"filename\":\"f.jpg\",\"type\":\"jpg\",\"api_key\":\"k\",\"force\":true}",
                resp, sizeof(resp));
    CHECK(strstr(resp, "\"code\":0") != NULL);
    CHECK(strstr(resp, "\"msg\":\"ok\"") != NULL);
    CHECK(strstr(g_all_sql, "桩描述") != NULL);
    return (g_fail > _fail0) ? 1 : 0;
}

/*
 * 降级不变式（对应 AI-DEF-001/002 的机制）：
 * 模型调用失败时接口仍返回 code 0，且描述退化为「<type>类型的文件：<文件名>」形式的兜底文案。
 * 期望行为应为显式失败；当前实现为静默降级，故这两条 CHECK 会 FAIL，以固化该缺陷。
 */
static int case_silent_degradation(void)
{
    char resp[1024] = {0};
    reset_stub_state();
    int _fail0 = g_fail;
    g_describe_ret = -1;      /* 模型调用失败 */
    CASE("AI-DEF-001/002 降级不变式：模型失败仍报成功且写入兜底描述");
    run_handler(handle_describe,
                "{\"user\":\"u\",\"token\":\"t\",\"md5\":\"m\",\"filename\":\"f.jpg\",\"type\":\"jpg\",\"api_key\":\"k\",\"force\":true}",
                resp, sizeof(resp));
    NOTE("响应=%s", resp);
    NOTE("落库 SQL(含描述)=%.200s", strstr(g_all_sql, "user_file_ai_desc") ? strstr(g_all_sql, "user_file_ai_desc") : g_all_sql);
    CHECK(strstr(resp, "\"code\":0") == NULL);        /* 期望：不应报成功 */
    CHECK(strstr(g_all_sql, "图片文件：") == NULL);    /* 期望：不应写兜底描述 */
    return (g_fail > _fail0) ? 1 : 0;
}

/* AIC-UT-009：search 缺 api_key */
static int case_search_missing_api_key(void)
{
    char resp[1024] = {0};
    reset_stub_state();
    int _fail0 = g_fail;
    CASE("AIC-UT-009 search 缺 api_key");
    run_handler(handle_search, "{\"user\":\"u\",\"token\":\"t\",\"query\":\"cat\"}",
                resp, sizeof(resp));
    CHECK(strstr(resp, "\"msg\":\"missing api_key\"") != NULL);
    return (g_fail > _fail0) ? 1 : 0;
}

/* AIC-UT-011：search token 非法 */
static int case_search_bad_token(void)
{
    char resp[1024] = {0};
    reset_stub_state();
    int _fail0 = g_fail;
    g_token_ret = -1;
    CASE("AIC-UT-011 search 非法 token -> code 4");
    run_handler(handle_search,
                "{\"user\":\"u\",\"token\":\"bad\",\"query\":\"cat\",\"api_key\":\"k\"}",
                resp, sizeof(resp));
    CHECK(strstr(resp, "\"code\":4") != NULL);
    return (g_fail > _fail0) ? 1 : 0;
}

/* AIC-UT-010：search 空查询 */
static int case_search_empty_query(void)
{
    char resp[1024] = {0};
    reset_stub_state();
    int _fail0 = g_fail;
    CASE("AIC-UT-010 search 空 query");
    run_handler(handle_search,
                "{\"user\":\"u\",\"token\":\"t\",\"query\":\"\",\"api_key\":\"k\"}",
                resp, sizeof(resp));
    CHECK(strstr(resp, "\"msg\":\"empty query\"") != NULL);
    return (g_fail > _fail0) ? 1 : 0;
}

/*
 * AI-DEF-008：用户无索引时的空结果与「无结果达阈值」不可区分——
 * 两者都返回 {"code":0,"count":0,"files":[]}，调用方无法得知是配置问题还是确实无匹配。
 */
static int case_search_empty_index(void)
{
    char resp[1024] = {0};
    reset_stub_state();
    int _fail0 = g_fail;
    g_faiss_ntotal = 0;
    CASE("AI-DEF-008 search 索引为空 -> 静默空结果，与无匹配不可区分");
    run_handler(handle_search,
                "{\"user\":\"u\",\"token\":\"t\",\"query\":\"cat\",\"api_key\":\"k\"}",
                resp, sizeof(resp));
    NOTE("响应=%s", resp);
    CHECK(strstr(resp, "\"count\":0") != NULL);
    CHECK(strstr(resp, "index") == NULL);   /* 期望：应能区分原因 */
    return (g_fail > _fail0) ? 1 : 0;
}

/* AIC-UT-012：rebuild 无需 api_key */
static int case_rebuild_ok(void)
{
    char resp[1024] = {0};
    reset_stub_state();
    int _fail0 = g_fail;
    CASE("AIC-UT-012 rebuild 无需 api_key");
    run_handler(handle_rebuild, "{\"user\":\"u\",\"token\":\"t\"}", resp, sizeof(resp));
    CHECK(strstr(resp, "\"code\":0") != NULL);
    CHECK(strstr(resp, "\"msg\":\"rebuilt\"") != NULL);
    CHECK(strstr(resp, "missing api_key") == NULL);
    return (g_fail > _fail0) ? 1 : 0;
}

/* AIC-UT-012b：rebuild token 非法 */
static int case_rebuild_bad_token(void)
{
    char resp[1024] = {0};
    reset_stub_state();
    int _fail0 = g_fail;
    g_token_ret = -1;
    CASE("AIC-UT-012b rebuild 非法 token -> code 4");
    run_handler(handle_rebuild, "{\"user\":\"u\",\"token\":\"bad\"}", resp, sizeof(resp));
    CHECK(strstr(resp, "\"code\":4") != NULL);
    return (g_fail > _fail0) ? 1 : 0;
}

/* AIC-UT-012c：rebuild 缺字段 */
static int case_rebuild_missing_fields(void)
{
    char resp[1024] = {0};
    reset_stub_state();
    int _fail0 = g_fail;
    CASE("AIC-UT-012c rebuild 缺字段");
    run_handler(handle_rebuild, "{\"user\":\"u\"}", resp, sizeof(resp));
    CHECK(strstr(resp, "\"msg\":\"missing fields\"") != NULL);
    return (g_fail > _fail0) ? 1 : 0;
}

/* ==================================================================== */

int main(void)
{
    setup_once();

    printf("AI 语义检索模块 C 单元测试\n");
    printf("被测源码：src_cgi/ai_cgi.cpp（static 函数直调，外部依赖全部打桩）\n");
    printf("打桩范围：verify_token / msql_conn / process_result_one / mysql_* / "
           "dashscope_* / faiss_*\n");

    RUN_ISOLATED("AIC-UT-006 invalid json", case_invalid_json);
    RUN_ISOLATED("AIC-UT-005 missing fields", case_missing_fields);
    RUN_ISOLATED("AIC-UT-003 missing api_key(describe)", case_missing_api_key_describe);
    RUN_ISOLATED("AIC-UT-004 bad token(describe)", case_bad_token_describe);
    RUN_ISOLATED("AIC-UT-007 file not found or no permission", case_file_not_found);
    RUN_ISOLATED("AIC-UT-008 already exists", case_already_exists);
    RUN_ISOLATED("AIC-UT-001(unit) describe ok path", case_describe_ok);
    RUN_ISOLATED("AI-DEF-001/002 silent degradation invariant", case_silent_degradation);
    RUN_ISOLATED("AIC-UT-009 search missing api_key", case_search_missing_api_key);
    RUN_ISOLATED("AIC-UT-011 search bad token", case_search_bad_token);
    RUN_ISOLATED("AIC-UT-010 search empty query", case_search_empty_query);
    RUN_ISOLATED("AI-DEF-008 search empty index", case_search_empty_index);
    RUN_ISOLATED("AIC-UT-012 rebuild ok", case_rebuild_ok);
    RUN_ISOLATED("AIC-UT-012b rebuild bad token", case_rebuild_bad_token);
    RUN_ISOLATED("AIC-UT-012c rebuild missing fields", case_rebuild_missing_fields);

    SUMMARY();
}

/* ==================================================================== */
/* 被测源码（置于末尾：其包含的 fcgi_stdio.h 会宏替换 stdio 符号）         */
/* ==================================================================== */

#define main ai_cgi_main
#include "ai_cgi.cpp"
#undef main

/*
 * 把 FCGI 的 stdin/stdout 槽位指回真实流，使被测代码的 FCGI_printf 输出
 * 能被 run_handler() 经 dup2 捕获。必须在包含 fcgi_stdio.h 之后定义。
 */
static void bind_fcgi_streams(void)
{
    _fcgi_sF[0].stdio_stream = g_real_stdin;
    _fcgi_sF[1].stdio_stream = g_real_stdout;
}

/*
 * 单元测试不依赖 /app/conf/cfg.json：该文件被 gitignore，镜像重建后可能不存在，
 * 且 read_cfg() 只在 main() 中调用（本测试不驱动 main）。
 * 因此把被测代码用到的路径与维度直接指向测试沙箱。
 * 这些是 ai_cgi.cpp 内的 static 全局，经 #include 后在本 TU 可见，
 * 必须在包含源码之后才能定义。
 */
static void setup_test_paths(void)
{
    mkdir("/tmp/ai_test_index", 0755);
    mkdir("/tmp/ai_test_index/users", 0755);
    mkdir("/tmp/ai_test_index/locks", 0755);

    snprintf(faiss_index_path, sizeof(faiss_index_path),
             "/tmp/ai_test_index/index.bin");
    snprintf(faiss_user_index_dir, sizeof(faiss_user_index_dir),
             "/tmp/ai_test_index/users");
    snprintf(faiss_lock_dir, sizeof(faiss_lock_dir),
             "/tmp/ai_test_index/locks");
    snprintf(web_server_ip, sizeof(web_server_ip), "172.30.0.3");
    snprintf(web_server_port, sizeof(web_server_port), "80");
    snprintf(embedding_model, sizeof(embedding_model), "text-embedding-v3");
    embedding_dimension = 1024;
}
