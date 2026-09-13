/*
 * upload_cgi.c 单元测试
 * 编译: gcc -Dmain=upload_cgi_main   (见 Makefile)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "mini_test.h"
#include "mocks.h"

#define DEF_BOUNDARY "------WebKitFormBoundaryTESTBOUNDARY"

/* ---- 被测函数 (upload_cgi.c 无头文件, 手工 extern) ---- */
extern int   bind_existing_file_to_user(char *user, char *filename, char *md5);
extern char *trim_space_and_around(char *begin, char *end);
extern char *buffer_search(char *buf, int total_len, const char *sep, const int seplen);
extern int   recv_save_file(long len, char *user, char *filename, char *md5, long *p_size);
extern int   store_fileinfo_to_mysql(char *user, char *filename, char *md5, long size,
                                     char *fileid, char *fdfs_file_url);

/* ============ 工具 ============ */

static void stdin_from(const char *path)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) { perror("open fixture"); _exit(99); }
    dup2(fd, STDIN_FILENO);
    close(fd);
    clearerr(stdin);
}

static void child_put(const char *k, const char *v)
{
    FILE *f = fopen(CHILD_KV_FILE, "a");
    if (f) { fprintf(f, "%s=%s\n", k, v); fclose(f); }
}

static int parent_get(const char *k, char *out, int outlen)
{
    FILE *f = fopen(CHILD_KV_FILE, "r");
    char line[1024];
    int found = -1;
    if (!f) return -1;
    while (fgets(line, sizeof(line), f)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        if (strcmp(line, k) == 0) {
            char *nl = strchr(eq + 1, '\n'); if (nl) *nl = '\0';
            snprintf(out, outlen, "%s", eq + 1);
            found = 0; break;
        }
    }
    fclose(f);
    return found;
}

static long write_raw(const char *path, const char *data, long len)
{
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    if (len < 0) len = (long)strlen(data);
    fwrite(data, 1, (size_t)len, f);
    fclose(f);
    return len;
}

/* 按 recv_save_file() 的解析顺序构造报文: file -> user -> md5 -> size */
static long make_body(const char *path,
                      const char *content, long content_len,
                      const char *user, const char *md5,
                      const char *fname, long declared_size,
                      const char *boundary)
{
    const char *B = boundary ? boundary : DEF_BOUNDARY;
    long n = -1;
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fprintf(f, "%s\r\n", B);
    fprintf(f, "Content-Disposition: form-data; name=\"file\"; filename=\"%s\"\r\n", fname);
    fprintf(f, "Content-Type: application/octet-stream\r\n");
    fprintf(f, "\r\n");
    fwrite(content, 1, (size_t)content_len, f);
    fprintf(f, "\r\n");
    fprintf(f, "%s\r\n", B);
    fprintf(f, "Content-Disposition: form-data; name=\"user\"\r\n");
    fprintf(f, "\r\n");
    fprintf(f, "%s\r\n", user);
    fprintf(f, "%s\r\n", B);
    fprintf(f, "Content-Disposition: form-data; name=\"md5\"\r\n");
    fprintf(f, "\r\n");
    fprintf(f, "%s\r\n", md5);
    fprintf(f, "%s\r\n", B);
    fprintf(f, "Content-Disposition: form-data; name=\"size\"\r\n");
    fprintf(f, "\r\n");
    fprintf(f, "%ld\r\n", declared_size);
    fprintf(f, "%s--\r\n", B);
    n = ftell(f);
    fclose(f);
    return n;
}

/* ============ 1. 纯函数 ============ */

static void test_trim_space_and_around(void)
{
    CASE("T01 trim_space_and_around");
    char a[] = "   \r\n  hello  ";
    CHECK_EQ_STR(trim_space_and_around(a, a + strlen(a)), "hello  ");
    char b[] = "  \r\n\t ";
    CHECK(trim_space_and_around(b, b + strlen(b)) == NULL);
    char c[] = "x   ";
    CHECK_EQ_STR(trim_space_and_around(c, c + strlen(c)), "x   ");
}

static void test_buffer_search(void)
{
    CASE("T02 buffer_search");
    char buf[] = "abcdefg";
    CHECK_EQ_INT((long)(buffer_search(buf, 7, "cd", 2) - buf), 2);
    CHECK(buffer_search(buf, 7, "zz", 2) == NULL);
    CHECK(buffer_search(buf, 0, "cd", 2) == NULL);   /* total_len==0 */
    CHECK(buffer_search(buf, 7, "cd", 0) == NULL);   /* seplen==0    */
    CHECK_EQ_INT((long)(buffer_search(buf, 7, "fg", 2) - buf), 5);
    CHECK(buffer_search(buf, 7, "gX", 2) == NULL);   /* 不得越界读   */
}

/* ============ 2. recv_save_file (隔离执行) ============ */

static int body_normal(void)
{
    const char *fx = "fixtures/ok.bin";
    const char *content = "hello yuncunchu\n";
    long len = make_body(fx, content, (long)strlen(content),
                         "alice", "d41d8cd98f00b204e9800998ecf8427e",
                         "note.txt", (long)strlen(content), NULL);
    char u[128] = {0}, fn[256] = {0}, m[256] = {0};
    long sz = 0;
    stdin_from(fx);
    int r = recv_save_file(len, u, fn, m, &sz);
    child_put("rc", r == 0 ? "0" : "-1");
    child_put("user", u);
    child_put("fname", fn);
    child_put("md5", m);
    char tmp[32]; snprintf(tmp, sizeof(tmp), "%ld", sz);
    child_put("size", tmp);
    unlink("note.txt");
    return 0;
}

static void test_recv_normal(void)
{
    CASE("T03 recv_save_file 正常报文  [TC-N-001][TC-M-002]");
    RUN_ISOLATED("recv_normal", body_normal);
    char v[512];
    if (parent_get("rc", v, sizeof(v)) == 0)     CHECK_EQ_INT(atoi(v), 0);
    if (parent_get("user", v, sizeof(v)) == 0)   CHECK_EQ_STR(v, "alice");
    if (parent_get("fname", v, sizeof(v)) == 0)  CHECK_EQ_STR(v, "note.txt");
    if (parent_get("size", v, sizeof(v)) == 0)   CHECK_EQ_INT(atoi(v), 16);
}

static int body_chinese_name(void)
{
    const char *fx = "fixtures/cn.bin";
    const char *content = "abc";
    long len = make_body(fx, content, 3, "alice", "0cc175b9c0f1b6a831c399e269772661",
                         "我的 文档-1.txt", 3, NULL);
    char u[128] = {0}, fn[256] = {0}, m[256] = {0};
    long sz = 0;
    stdin_from(fx);
    recv_save_file(len, u, fn, m, &sz);
    child_put("fname", fn);
    unlink("我的 文档-1.txt");
    return 0;
}

static void test_recv_chinese_name(void)
{
    CASE("T04 中文/空格文件名  [TC-N-003]");
    RUN_ISOLATED("recv_chinese_name", body_chinese_name);
    char v[512];
    if (parent_get("fname", v, sizeof(v)) == 0) CHECK_EQ_STR(v, "我的 文档-1.txt");
}

static int body_size_mismatch(void)
{
    const char *fx = "fixtures/badsize.bin";
    const char *content = "12345";
    long len = make_body(fx, content, 5, "alice", "deadbeef", "x.txt", 999, NULL);
    char u[128] = {0}, fn[256] = {0}, m[256] = {0};
    long sz = 0;
    stdin_from(fx);
    int r = recv_save_file(len, u, fn, m, &sz);
    child_put("rc", r == 0 ? "0" : "-1");
    unlink("x.txt");
    return 0;
}

static void test_recv_size_mismatch(void)
{
    CASE("T05 声明 size 与内容不符 -> 必须失败  [TC-S-005]");
    RUN_ISOLATED("recv_size_mismatch", body_size_mismatch);
    char v[512];
    if (parent_get("rc", v, sizeof(v)) == 0) CHECK_EQ_INT(atoi(v), -1);
}

static int body_zero_byte(void)
{
    const char *fx = "fixtures/zero.bin";
    long len = make_body(fx, "", 0, "alice", "d41d8cd98f00b204e9800998ecf8427e",
                         "empty.txt", 0, NULL);
    char u[128] = {0}, fn[256] = {0}, m[256] = {0};
    long sz = 0;
    stdin_from(fx);
    int r = recv_save_file(len, u, fn, m, &sz);
    child_put("rc", r == 0 ? "0" : "-1");
    unlink("empty.txt");
    return 0;
}

static void test_recv_zero_byte(void)
{
    CASE("T06 0 字节文件  [TC-B-001]");
    RUN_ISOLATED("recv_zero_byte", body_zero_byte);
    char v[512];
    if (parent_get("rc", v, sizeof(v)) == 0)
        NOTE("0 字节实际返回 rc=%s (需求未定, 见矩阵 2.2 待确认项)", v);
}

/* ---- 以下为"预期崩溃/越权"的缺陷探测用例 ---- */

static int bad_missing_filename(void)
{
    const char *fx = "fixtures/no_filename.bin";
    /* 第 2 段没有 filename= 字段 */
    long len = write_raw(fx,
        DEF_BOUNDARY "\r\n"
        "Content-Disposition: form-data; name=\"file\"\r\n"
        "Content-Type: application/octet-stream\r\n"
        "\r\n"
        "hello\r\n"
        DEF_BOUNDARY "--\r\n", -1);
    char u[128] = {0}, fn[256] = {0}, m[256] = {0};
    long sz = 0;
    stdin_from(fx);
    int r = recv_save_file(len, u, fn, m, &sz);
    child_put("rc", r == 0 ? "0" : "-1");
    return 1;   /* 没崩 -> 记为 FAIL(但本用例期望的正是崩溃) */
}

static void test_recv_missing_filename(void)
{
    CASE("T07 报文缺 filename= -> 期望崩溃  [TC-N-004][RK-10]");
    RUN_ISOLATED("recv_missing_filename", bad_missing_filename);
}

static int bad_missing_md5(void)
{
    const char *fx = "fixtures/no_md5.bin";
    long len = write_raw(fx,
        DEF_BOUNDARY "\r\n"
        "Content-Disposition: form-data; name=\"file\"; filename=\"a.txt\"\r\n"
        "Content-Type: application/octet-stream\r\n"
        "\r\n"
        "hello\r\n"
        DEF_BOUNDARY "\r\n"
        "Content-Disposition: form-data; name=\"user\"\r\n"
        "\r\n"
        "alice\r\n"
        DEF_BOUNDARY "--\r\n", -1);
    char u[128] = {0}, fn[256] = {0}, m[256] = {0};
    long sz = 0;
    stdin_from(fx);
    recv_save_file(len, u, fn, m, &sz);
    unlink("a.txt");
    return 1;
}

static void test_recv_missing_md5(void)
{
    CASE("T08 报文缺 name=\"md5\" -> 期望崩溃  [RK-10]");
    RUN_ISOLATED("recv_missing_md5", bad_missing_md5);
}

static int bad_truncated(void)
{
    const char *fx = "fixtures/truncated.bin";
    /* 有开头, 无结尾 boundary -> buffer_search 返回 NULL */
    long len = write_raw(fx,
        DEF_BOUNDARY "\r\n"
        "Content-Disposition: form-data; name=\"file\"; filename=\"a.txt\"\r\n"
        "Content-Type: application/octet-stream\r\n"
        "\r\n"
        "hello world without trailing boundary", -1);
    char u[128] = {0}, fn[256] = {0}, m[256] = {0};
    long sz = 0;
    stdin_from(fx);
    recv_save_file(len, u, fn, m, &sz);
    return 1;
}

static void test_recv_truncated(void)
{
    CASE("T09 报文截断(无结束 boundary) -> 期望崩溃  [RK-10]");
    RUN_ISOLATED("recv_truncated", bad_truncated);
}

static int bad_long_boundary(void)
{
    const char *fx = "fixtures/long_boundary.bin";
    static char big[1200];
    memset(big, 'B', 1100);                     /* 首行 1100 字节 >> boundary[512] */
    big[1100] = '\0';
    const char *content = "hi";
    long len = make_body(fx, content, 2, "alice", "x", "a.txt", 2, big);
    char u[128] = {0}, fn[256] = {0}, m[256] = {0};
    long sz = 0;
    stdin_from(fx);
    recv_save_file(len, u, fn, m, &sz);
    unlink("a.txt");
    return 1;
}

static void test_recv_long_boundary(void)
{
    CASE("T10 分界线超长(1100B) -> 期望栈溢出崩溃  [RK-10]");
    RUN_ISOLATED("recv_long_boundary", bad_long_boundary);
}

static int bad_long_filename(void)
{
    const char *fx = "fixtures/long_name.bin";
    static char big[1200];
    memset(big, 'n', 1000);
    big[1000] = '\0';                           /* 文件名 1000B >> filename[256] */
    const char *content = "hi";
    long len = make_body(fx, content, 2, "alice", "x", big, 2, NULL);
    char u[128] = {0};
    char fn[256] = {0};                         /* 与 main() 中 FILE_NAME_LEN 一致 */
    char m[256] = {0};
    long sz = 0;
    stdin_from(fx);
    recv_save_file(len, u, fn, m, &sz);
    return 1;
}

static void test_recv_long_filename(void)
{
    CASE("T11 超长文件名(1000B) -> 期望栈溢出崩溃  [TC-N-004]");
    RUN_ISOLATED("recv_long_filename", bad_long_filename);
}

static int bad_path_traversal(void)
{
    mkdir("sandbox", 0755);
    if (chdir("sandbox") != 0) return 1;

    const char *fx = "fx_trav.bin";
    const char *content = "pwn";
    long len = make_body(fx, content, 3, "alice", "x",
                         "../escaped_by_upload.txt", 3, NULL);
    char u[128] = {0}, fn[256] = {0}, m[256] = {0};
    long sz = 0;
    stdin_from(fx);
    recv_save_file(len, u, fn, m, &sz);
    chdir("..");

    /* 若文件落到 sandbox 之外 -> 路径穿越成立 */
    if (access("escaped_by_upload.txt", F_OK) == 0) {
        printf("       [VULN] 文件被写到工作目录之外: ../escaped_by_upload.txt\n");
        unlink("escaped_by_upload.txt");
        return 1;
    }
    return 0;
}

static void test_recv_path_traversal(void)
{
    CASE("T12 文件名 ../ 路径穿越  [TC-S-006][RK-10]");
    RUN_ISOLATED("recv_path_traversal", bad_path_traversal);
}

/* ============ 3. bind_existing_file_to_user (MySQL 桩) ============ */

static void test_bind_md5_not_exists(void)
{
    CASE("T13 MD5 不存在 -> 返回 1 继续物理上传  [TC-M-001]");
    mock_reset();
    mock_result_ret = 1;                    /* file_info 查不到 */
    CHECK_EQ_INT(bind_existing_file_to_user("alice", "a.txt", "aaa"), 1);
}

static void test_bind_user_already_owned(void)
{
    CASE("T14 当前用户已拥有该文件 -> 返回 2  [TC-M-005]");
    mock_reset();
    mock_result_ret = 0;                    /* file_info 命中 */
    strcpy(mock_result_val, "3");
    mock_result_queue[0] = 0;               /* user_file_list 也命中 -> ret=2 */
    mock_result_queue_len = 1;
    CHECK_EQ_INT(bind_existing_file_to_user("alice", "a.txt", "aaa"), 2);
}

static void test_bind_reuse_ok(void)
{
    CASE("T15 秒传复用成功 -> 返回 0 且写入 user_file_list  [TC-M-003][TC-M-004]");
    mock_reset();
    mock_result_ret = 0;
    strcpy(mock_result_val, "7");
    mock_result_queue[0] = 0;   /* file_info: count=7 */
    mock_result_queue[1] = 1;   /* user_file_list: 不存在 */
    mock_result_queue[2] = 0;   /* user_file_count: count=7 */
    mock_result_queue_len = 3;
    CHECK_EQ_INT(bind_existing_file_to_user("alice", "a.txt", "aaa"), 0);
    CHECK(mock_sql_seen("update file_info set count = 8"));
    CHECK(mock_sql_seen("insert into user_file_list"));
}

static void test_bind_conn_fail(void)
{
    CASE("T16 数据库连不上 -> 返回 -1, 不崩  [RK-11]");
    mock_reset();
    mock_conn_fail = 1;
    CHECK_EQ_INT(bind_existing_file_to_user("alice", "a.txt", "aaa"), -1);
}
  static void test_bind_sql_injection(void)
  {
      CASE("T17 文件名含单引号 -> SQL 是否被转义  [RK-10]");
      mock_reset();
      /* 必须让第 1 次查询命中(0)、第 2 次未命中(1),
       * 才能走到携带 filename 的那条 SQL */
      mock_result_queue[0] = 0;            /* file_info 命中 */
      mock_result_queue[1] = 1;            /* user_file_list 未命中 */
      mock_result_queue_len = 2;
      strcpy(mock_result_val, "1");        /* count = 1 */

      bind_existing_file_to_user("alice", "a'; drop table file_info; --", "aaa");

      printf("       ---- 截获的 SQL ----\n");
      for (int i = 0; i < mock_sql_count && i < MOCK_SQL_SLOTS; i++)
          printf("       [%d] %s\n", i, mock_sql_log[i]);

      if (mock_sql_seen("drop table file_info")) {
          printf("       [VULN] 恶意文件名原样拼进 SQL, 未做任何转义\n");
          g_fail++;
      } else {
          printf("  PASS  SQL 中的文件名已被转义/过滤\n");
          g_pass++;
      }
  }




  static void test_bind_lost_update(void)
  {
      CASE("T21 引用计数丢失更新: 相同初值产生相同 UPDATE  [RK-03][TC-P-003]");

      for (int round = 0; round < 2; round++) {
          mock_reset();
          mock_result_queue[0] = 0;              /* file_info 命中 */
          mock_result_queue[1] = 1;              /* user_file_list 未命中 */
          mock_result_queue_len = 2;
          strcpy(mock_result_val, "7");          /* 两轮都模拟"读到 count = 7" */

          bind_existing_file_to_user("testuser", "a.txt", "aaa");

          for (int i = 0; i < mock_sql_count && i < MOCK_SQL_SLOTS; i++)
              if (strstr(mock_sql_log[i], "update file_info"))
                  printf("       round %d -> %s\n", round, mock_sql_log[i]);
      }

      printf("       [VULN] 两轮都生成 count = 8 而非 8 和 9\n");
      printf("              新值由客户端计算, 并发下必然丢失更新\n");
      g_fail++;
  }




/* ============ 4. store_fileinfo_to_mysql ============ */

static void test_store_user_dup_skip(void)
{
    CASE("T18 用户已有该文件 -> 跳过插入, 不产生重复行  [TC-M-005]");
    mock_reset();
    mock_result_ret = 0;                    /* user_file_list 命中 -> 早退 */
    strcpy(mock_result_val, "0");
    CHECK_EQ_INT(store_fileinfo_to_mysql("alice", "a.txt", "aaa", 10,
                                         "/group1/M00/x", "http://h/x"), 0);
    CHECK(!mock_sql_seen("insert into user_file_list"));
}

static void test_store_new_file(void)
{
    CASE("T19 新文件 -> 写入 file_info + user_file_list  [TC-D-001]");
    mock_reset();
    mock_result_queue[0] = 1;   /* user_file_list: 无记录 */
    mock_result_queue[1] = 1;   /* file_info: 无记录 -> insert */
    mock_result_queue[2] = 1;   /* user_file_count: 无记录 -> insert */
    mock_result_queue_len = 3;
    CHECK_EQ_INT(store_fileinfo_to_mysql("alice", "a.txt", "aaa", 10,
                                         "/group1/M00/x", "http://h/x"), 0);
    CHECK(mock_sql_seen("insert into file_info"));
    CHECK(mock_sql_seen("insert into user_file_list"));
    CHECK(mock_sql_seen("insert into user_file_count"));
}

static void test_store_db_fail(void)
{
    CASE("T20 DB 写入失败 -> 返回 -1  [TC-D-003]");
    mock_reset();
    mock_result_queue[0] = 1;
    mock_result_queue[1] = 1;
    mock_result_queue_len = 2;
    mock_query_fail = 1;
    CHECK_EQ_INT(store_fileinfo_to_mysql("alice", "a.txt", "aaa", 10,
                                         "/group1/M00/x", "http://h/x"), -1);
}

/* ============ main ============ */

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    if (chdir("/app") != 0) { perror("chdir /app"); return 2; }
    mkdir("test_upload/fixtures", 0755);
    if (chdir("test_upload") != 0) { perror("chdir test_upload"); return 2; }
    mkdir("fixtures", 0755);

    printf("=== upload_cgi.c 单元测试 ===\n");

    test_trim_space_and_around();
    test_buffer_search();
    test_recv_normal();
    test_recv_chinese_name();
    test_recv_size_mismatch();
    test_recv_zero_byte();
    test_recv_missing_filename();
    test_recv_missing_md5();
    test_recv_truncated();
    test_recv_long_boundary();
    test_recv_long_filename();
    test_recv_path_traversal();
    test_bind_md5_not_exists();
    test_bind_user_already_owned();
    test_bind_reuse_ok();
    test_bind_conn_fail();
    test_bind_sql_injection();
    test_store_user_dup_skip();
    test_store_new_file();
    test_store_db_fail();
    test_bind_lost_update();


    SUMMARY();
}
