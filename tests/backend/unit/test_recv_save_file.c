#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "mini_test.h"
#include "multipart_fixture.h"
#include "util_cgi.h"

/* 被测函数来自 src_cgi/upload_cgi.c。 */
int recv_save_file(long len, char *user, char *filename, char *md5, long *p_size);

/*
 * 三个输出缓冲区必须与生产调用方 main() 中的声明保持一致
 * （upload_cgi.c:1029~1031 使用 FILE_NAME_LEN / USER_NAME_LEN / MD5_LEN）。
 * 否则"超长字段"类用例会因为缓冲区大小与生产不同而给出失真的结论。
 */
_Static_assert(USER_NAME_LEN == 128, "生产用户名缓冲区长度已变化，请同步测试缓冲区");
_Static_assert(FILE_NAME_LEN == 256, "生产文件名缓冲区长度已变化，请同步测试缓冲区");
_Static_assert(MD5_LEN == 256, "生产 MD5 缓冲区长度已变化，请同步测试缓冲区");

typedef struct {
    const unsigned char *content;
    size_t content_size;
    const char *filename;
    const char *user;
    const char *md5;
    long declared_size;
    int allow_explicit_rejection;
    const char *case_id;
} valid_case;

/*
 * 所有用例统一使用 /tmp/upload_cgi_<用例ID> 这一固定沙箱，而不是 mkdtemp 随机目录。
 * 子进程可能被信号终止或被 Sanitizer 中止，来不及执行自身清理；
 * 只有固定且可预测的路径才能让父进程在 RUN_ISOLATED 之后兜底回收。
 */
static void case_paths(const char *case_id,
                       char *sandbox,
                       size_t sandbox_size,
                       char *request,
                       size_t request_size)
{
    snprintf(sandbox, sandbox_size, "/tmp/upload_cgi_%s", case_id);
    snprintf(request, request_size, "%s/request.bin", sandbox);
}

/*
 * 清理本用例自己的沙箱目录。
 *
 * 不能只按固定文件名 unlink：UT-MP-022 的栈溢出会改写调用方的文件名缓冲区，
 * 被测函数可能以完全不同的名字落盘。因此这里只在本用例自己的沙箱目录
 * /tmp/upload_cgi_<用例ID> 内逐个 unlink **普通文件**，随后 rmdir；
 * 不递归进入子目录，也不触碰沙箱之外的任何路径。
 *
 * 父子进程都可调用；即使用例崩溃，父进程仍能按固定路径回收沙箱。
 */
static void cleanup_case(const char *case_id)
{
    char sandbox[256];
    char request[320];
    DIR *dir;
    struct dirent *entry;

    case_paths(case_id, sandbox, sizeof(sandbox), request, sizeof(request));

    dir = opendir(sandbox);
    if (dir) {
        while ((entry = readdir(dir)) != NULL) {
            char path[512];
            struct stat st;
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                continue;
            snprintf(path, sizeof(path), "%s/%s", sandbox, entry->d_name);
            if (lstat(path, &st) == 0 && S_ISREG(st.st_mode))
                unlink(path);
        }
        closedir(dir);
    }
    rmdir(sandbox);
}

static void cleanup_malformed_case(const char *case_id)
{
    cleanup_case(case_id);
}

static int prepare_case(const char *case_id)
{
    char sandbox[256];
    char request[320];
    case_paths(case_id, sandbox, sizeof(sandbox), request, sizeof(request));
    cleanup_case(case_id);
    if (mkdir(sandbox, 0700) != 0) return -1;
    if (chdir(sandbox) != 0) return -1;
    return 0;
}

static int prepare_malformed_case(const char *case_id)
{
    return prepare_case(case_id);
}

/*
 * 通用畸形用例驱动：报文各字段与 boundary 均可自定义。
 * 返回值语义同 run_valid_case：0 表示"按要求安全拒绝且无残留"。
 */
static int run_rejected_message(const char *case_id,
                                const char *user,
                                const char *md5,
                                const char *filename,
                                long declared_size,
                                const char *boundary,
                                const multipart_body_options *options)
{
    static const unsigned char content[] = "abcde";
    char parsed_user[USER_NAME_LEN] = {0};
    char parsed_filename[FILE_NAME_LEN] = {0};
    char parsed_md5[MD5_LEN] = {0};
    long parsed_size = -1;
    long body_size;
    int result;
    int rc = 0;

    if (prepare_malformed_case(case_id) != 0) return 90;
    body_size = multipart_write_body_with_options(
        "request.bin", content, sizeof(content) - 1,
        user, md5, filename, declared_size,
        boundary, options);
    if (body_size < 0) return 91;
    if (multipart_redirect_stdin("request.bin") != 0) return 92;

    result = recv_save_file(body_size, parsed_user, parsed_filename,
                            parsed_md5, &parsed_size);
    if (result != -1) rc = 1;
    if (access(filename, F_OK) == 0) rc = 2;

    unlink("request.bin");
    unlink(filename);
    if (chdir("/tmp") == 0) cleanup_malformed_case(case_id);
    return rc;
}

static int run_rejected_options(const char *case_id,
                                const multipart_body_options *options,
                                const char *filename,
                                long declared_size)
{
    return run_rejected_message(case_id, "alice",
                                "ab56b4d92b40713acc5af89985d4b786",
                                filename, declared_size, NULL, options);
}

static int run_malformed_options(const char *case_id,
                                 const multipart_body_options *options)
{
    return run_rejected_options(case_id, options, "bad.bin", 5);
}

static int run_malformed_raw(const char *case_id,
                             const unsigned char *body,
                             size_t body_size)
{
    char parsed_user[USER_NAME_LEN] = {0};
    char parsed_filename[FILE_NAME_LEN] = {0};
    char parsed_md5[MD5_LEN] = {0};
    long parsed_size = -1;
    int result;
    int rc = 0;

    if (prepare_malformed_case(case_id) != 0) return 90;
    if (multipart_write_bytes("request.bin", body, body_size) < 0) return 91;
    if (multipart_redirect_stdin("request.bin") != 0) return 92;

    result = recv_save_file((long)body_size, parsed_user, parsed_filename,
                            parsed_md5, &parsed_size);
    if (result != -1) rc = 1;
    if (access("bad.bin", F_OK) == 0) rc = 2;

    unlink("request.bin");
    unlink("bad.bin");
    if (chdir("/tmp") == 0) cleanup_malformed_case(case_id);
    return rc;
}

/*
 * 每个用例都在独立子进程和固定临时目录中运行：
 * 1. 动态生成 multipart 报文；2. 重定向 stdin；3. 调用真实解析函数；
 * 4. 校验输出字段与落盘内容；5. 清理测试文件。
 * 收敛动作在本进程和父进程各做一次，子进程被异常终止时也不会留下沙箱。
 */
static int run_valid_case(const valid_case *item)
{
    char parsed_user[USER_NAME_LEN] = {0};
    char parsed_filename[FILE_NAME_LEN] = {0};
    char parsed_md5[MD5_LEN] = {0};
    long parsed_size = -1;
    long body_size;
    int result;
    int rc = 0;

    if (prepare_case(item->case_id) != 0) return 90;

    body_size = multipart_write_body("request.bin",
                                     item->content,
                                     item->content_size,
                                     item->user,
                                     item->md5,
                                     item->filename,
                                     item->declared_size,
                                     NULL);
    if (body_size < 0) return 91;
    if (multipart_redirect_stdin("request.bin") != 0) return 92;

    result = recv_save_file(body_size,
                            parsed_user,
                            parsed_filename,
                            parsed_md5,
                            &parsed_size);

    if (result == -1 && item->allow_explicit_rejection) {
        NOTE("当前实现对 0 字节文件返回 -1；需求确认后应将此用例固定为唯一预期");
        if (access(item->filename, F_OK) == 0) rc = 7;
        goto CLEANUP;
    }

    if (result != 0) rc = 1;
    else if (strcmp(parsed_user, item->user) != 0) rc = 2;
    else if (strcmp(parsed_filename, item->filename) != 0) rc = 3;
    else if (strcmp(parsed_md5, item->md5) != 0) rc = 4;
    else if (parsed_size != item->declared_size) rc = 5;
    else if (!multipart_file_equals(item->filename,
                                    item->content,
                                    item->content_size)) rc = 6;

CLEANUP:
    unlink("request.bin");
    unlink(item->filename);
    if (chdir("/tmp") == 0) cleanup_case(item->case_id);
    return rc;
}

/* UT-MP-001 / TC-N-001：正常文本报文应正确解析四个字段并保存文件。 */
static int test_normal_text_multipart(void)
{
    static const unsigned char content[] = "hello yuncunchu\n";
    const valid_case item = {
        content, sizeof(content) - 1, "note.txt", "alice",
        "3490f59b2b071332631aaa8e65cbd8f1", (long)(sizeof(content) - 1), 0, "UT-MP-001"
    };
    return run_valid_case(&item);
}

/* UT-MP-002 / TC-N-002：二进制内容中的 \0 不应导致内容被截断。 */
static int test_binary_content_with_zero_bytes(void)
{
    static const unsigned char content[] = {0x00, 0x01, 'A', 0x00, 'B', 0xff};
    const valid_case item = {
        content, sizeof(content), "binary.dat", "alice",
        "72cdb1041b332428c1061394945ea8f7", (long)sizeof(content), 0, "UT-MP-002"
    };
    return run_valid_case(&item);
}

/* UT-MP-003 / TC-N-003：UTF-8 中文和空格文件名应保持原样。 */
static int test_chinese_and_space_filename(void)
{
    static const unsigned char content[] = "unicode filename";
    const valid_case item = {
        content, sizeof(content) - 1, "我的 文档-1.txt", "alice",
        "9d4ba1f1c835ec52ad3b445f0ab7c98f", (long)(sizeof(content) - 1), 0, "UT-MP-003"
    };
    return run_valid_case(&item);
}

/* UT-MP-004 / TC-B-001：需求未定，记录接受或明确拒绝行为，并确保不崩溃、不残留。 */
static int test_zero_byte_file_current_baseline(void)
{
    static const unsigned char empty_content[] = {0};
    const valid_case item = {
        empty_content, 0, "empty.bin", "alice",
        "d41d8cd98f00b204e9800998ecf8427e", 0, 1, "UT-MP-004"
    };
    return run_valid_case(&item);
}

/* UT-MP-005 / TC-S-005：声明 size 与实际字节数相同时，p_size 应精确匹配。 */
static int test_declared_size_matches_content(void)
{
    static const unsigned char content[] = "123456789";
    const valid_case item = {
        content, sizeof(content) - 1, "size-check.bin", "bob",
        "25f9e794323b453885f5181f1b624d0b", 9, 0, "UT-MP-005"
    };
    return run_valid_case(&item);
}

static int test_missing_opening_boundary(void)
{
    multipart_body_options options = multipart_default_options();
    options.include_opening_boundary = 0;
    return run_malformed_options("UT-MP-006", &options);
}

static int test_missing_closing_boundary(void)
{
    multipart_body_options options = multipart_default_options();
    options.include_closing_boundary = 0;
    return run_malformed_options("UT-MP-007", &options);
}

static int test_missing_file_disposition(void)
{
    multipart_body_options options = multipart_default_options();
    options.include_file_disposition = 0;
    return run_malformed_options("UT-MP-008", &options);
}

static int test_missing_filename(void)
{
    multipart_body_options options = multipart_default_options();
    options.include_filename = 0;
    return run_malformed_options("UT-MP-009", &options);
}

static int test_missing_user(void)
{
    multipart_body_options options = multipart_default_options();
    options.include_user = 0;
    return run_malformed_options("UT-MP-010", &options);
}

static int test_missing_md5(void)
{
    multipart_body_options options = multipart_default_options();
    options.include_md5 = 0;
    return run_malformed_options("UT-MP-011", &options);
}

static int test_missing_size(void)
{
    multipart_body_options options = multipart_default_options();
    options.include_size = 0;
    return run_malformed_options("UT-MP-012", &options);
}

/* UT-MP-013：声明大小小于实际内容时必须拒绝，不能静默截断。 */
static int test_declared_size_smaller_than_content(void)
{
    multipart_body_options options = multipart_default_options();
    return run_rejected_options("UT-MP-013", &options, "bad.bin", 4);
}

/* UT-MP-014：声明大小大于实际内容时必须拒绝，不能越界读取。 */
static int test_declared_size_larger_than_content(void)
{
    multipart_body_options options = multipart_default_options();
    return run_rejected_options("UT-MP-014", &options, "bad.bin", 6);
}

static int test_negative_size(void)
{
    multipart_body_options options = multipart_default_options();
    options.size_text_override = "-1";
    return run_rejected_options("UT-MP-015a", &options, "bad.bin", 5);
}

static int test_non_numeric_size(void)
{
    multipart_body_options options = multipart_default_options();
    options.size_text_override = "not-a-number";
    return run_rejected_options("UT-MP-015b", &options, "bad.bin", 5);
}

static int test_overflow_size(void)
{
    multipart_body_options options = multipart_default_options();
    options.size_text_override = "999999999999999999999999999999999999";
    return run_rejected_options("UT-MP-015c", &options, "bad.bin", 5);
}

/*
 * UT-MP-016 / TC-N-004：超长文件名应安全拒绝，不得破坏栈。
 * 文件名长度取 1000 > FILE_NAME_LEN(256)，与生产 main() 的缓冲区容量对齐；
 * 断言的目标是"拒绝且不越界"，而不是断言具体的越界后果。
 */
static int test_overlong_filename(void)
{
    multipart_body_options options = multipart_default_options();
    char filename[1001];
    memset(filename, 'a', sizeof(filename) - 1);
    filename[sizeof(filename) - 1] = '\0';
    return run_rejected_options("UT-MP-016", &options, filename, 5);
}

/*
 * UT-MP-017 / TC-S-006：文件名不得将文件写出测试沙箱。
 * 沙箱为 /tmp/upload_cgi_UT-MP-017，因此 "../escaped_by_upload_test.txt" 会落在
 * 沙箱父目录 /tmp；run_rejected_options() 的 access()/unlink() 均相对于沙箱 cwd 解析，
 * 可正确命中该路径。用例结束后由 main() 再次 unlink 兜底。
 */
static int test_parent_directory_filename(void)
{
    multipart_body_options options = multipart_default_options();
    unlink("/tmp/escaped_by_upload_test.txt");
    return run_rejected_options("UT-MP-017", &options,
                                "../escaped_by_upload_test.txt", 5);
}

/* UT-MP-018：内容中的 boundary 字节不能被误判为真正的分隔行。 */
static int test_boundary_bytes_inside_content(void)
{
    static const unsigned char content[] =
        "prefix" TEST_MULTIPART_BOUNDARY "suffix";
    const valid_case item = {
        content, sizeof(content) - 1, "boundary-content.bin", "alice",
        "8dc16e26f65a31a56c7c0a7e1b1e938e", (long)(sizeof(content) - 1), 0, "UT-MP-018"
    };
    return run_valid_case(&item);
}

/* UT-MP-019：字段乱序允许明确拒绝，但不得崩溃或误落盘。 */
static int test_reordered_metadata_fields(void)
{
    static const unsigned char body[] =
        TEST_MULTIPART_BOUNDARY "\r\n"
        "Content-Disposition: form-data; name=\"file\"; filename=\"bad.bin\"\r\n"
        "Content-Type: application/octet-stream\r\n\r\n"
        "abcde\r\n"
        TEST_MULTIPART_BOUNDARY "\r\n"
        "Content-Disposition: form-data; name=\"md5\"\r\n\r\n"
        "ab56b4d92b40713acc5af89985d4b786\r\n"
        TEST_MULTIPART_BOUNDARY "\r\n"
        "Content-Disposition: form-data; name=\"user\"\r\n\r\nalice\r\n"
        TEST_MULTIPART_BOUNDARY "\r\n"
        "Content-Disposition: form-data; name=\"size\"\r\n\r\n5\r\n"
        TEST_MULTIPART_BOUNDARY "--\r\n";
    return run_malformed_raw("UT-MP-019", body, sizeof(body) - 1);
}

/*
 * UT-MP-021：boundary 行超过 TEMP_BUF_MAX_LEN(512)。
 * upload_cgi.c:270 的 strncpy(boundary, begin, p1-begin) 没有长度上限，
 * 首行长度完全由请求方决定，应当安全拒绝而不是破坏栈。
 */
static int test_overlong_boundary_line(void)
{
    multipart_body_options options = multipart_default_options();
    static char boundary[607];
    memset(boundary, '-', sizeof(boundary) - 1);
    boundary[sizeof(boundary) - 1] = '\0';
    return run_rejected_message("UT-MP-021", "alice",
                                "ab56b4d92b40713acc5af89985d4b786",
                                "bad.bin", 5, boundary, &options);
}

/*
 * UT-MP-022：user 值超过 USER_NAME_LEN(128)。
 * upload_cgi.c:320 的 strncpy(user, p3, end-p3) 没有长度上限。
 */
static int test_overlong_user_value(void)
{
    multipart_body_options options = multipart_default_options();
    static char user[201];
    memset(user, 'u', sizeof(user) - 1);
    user[sizeof(user) - 1] = '\0';
    return run_rejected_message("UT-MP-022", user,
                                "ab56b4d92b40713acc5af89985d4b786",
                                "bad.bin", 5, NULL, &options);
}

/*
 * UT-MP-023：md5 值超过 MD5_LEN(256)。
 * upload_cgi.c:330 的 strncpy(md5, p4, end-p4) 没有长度上限。
 */
static int test_overlong_md5_value(void)
{
    multipart_body_options options = multipart_default_options();
    static char md5[401];
    memset(md5, 'a', sizeof(md5) - 1);
    md5[sizeof(md5) - 1] = '\0';
    return run_rejected_message("UT-MP-023", "alice", md5,
                                "bad.bin", 5, NULL, &options);
}

/*
 * UT-MP-024：size 文本超过 size_text[64+1]。
 * upload_cgi.c:352 的 strncpy(size_text, p5, end-p5) 没有长度上限。
 */
static int test_overlong_size_text(void)
{
    multipart_body_options options = multipart_default_options();
    static char size_text[101];
    memset(size_text, '9', sizeof(size_text) - 1);
    size_text[sizeof(size_text) - 1] = '\0';
    options.size_text_override = size_text;
    return run_rejected_message("UT-MP-024", "alice",
                                "ab56b4d92b40713acc5af89985d4b786",
                                "bad.bin", 5, NULL, &options);
}

static int test_truncated_after_opening_boundary(void)
{
    static const unsigned char body[] =
        TEST_MULTIPART_BOUNDARY "\r\nContent-Dis";
    return run_malformed_raw("UT-MP-020a", body, sizeof(body) - 1);
}

static int test_truncated_in_file_content(void)
{
    static const unsigned char body[] =
        TEST_MULTIPART_BOUNDARY "\r\n"
        "Content-Disposition: form-data; name=\"file\"; filename=\"bad.bin\"\r\n"
        "Content-Type: application/octet-stream\r\n\r\n"
        "abcde";
    return run_malformed_raw("UT-MP-020b", body, sizeof(body) - 1);
}

static int test_truncated_in_size_value(void)
{
    static const unsigned char body[] =
        TEST_MULTIPART_BOUNDARY "\r\n"
        "Content-Disposition: form-data; name=\"file\"; filename=\"bad.bin\"\r\n"
        "Content-Type: application/octet-stream\r\n\r\n"
        "abcde\r\n"
        TEST_MULTIPART_BOUNDARY "\r\n"
        "Content-Disposition: form-data; name=\"user\"\r\n\r\nalice\r\n"
        TEST_MULTIPART_BOUNDARY "\r\n"
        "Content-Disposition: form-data; name=\"md5\"\r\n\r\n"
        "ab56b4d92b40713acc5af89985d4b786\r\n"
        TEST_MULTIPART_BOUNDARY "\r\n"
        "Content-Disposition: form-data; name=\"size\"\r\n\r\n5";
    return run_malformed_raw("UT-MP-020c", body, sizeof(body) - 1);
}

int main(void)
{
    CASE("UT-MP-001: 正常文本 multipart 报文");
    RUN_ISOLATED("normal text multipart", test_normal_text_multipart);
    cleanup_case("UT-MP-001");

    CASE("UT-MP-002: 文件内容包含二进制零字节");
    RUN_ISOLATED("binary content", test_binary_content_with_zero_bytes);
    cleanup_case("UT-MP-002");

    CASE("UT-MP-003: 中文和空格文件名");
    RUN_ISOLATED("unicode filename", test_chinese_and_space_filename);
    cleanup_case("UT-MP-003");

    CASE("UT-MP-004: 0 字节文件当前行为基线");
    NOTE("当前需求未最终确认；本测试记录实际行为，并检查进程安全与文件残留");
    RUN_ISOLATED("zero-byte file", test_zero_byte_file_current_baseline);
    cleanup_case("UT-MP-004");

    CASE("UT-MP-005: 声明大小与实际内容一致");
    RUN_ISOLATED("declared size matches", test_declared_size_matches_content);
    cleanup_case("UT-MP-005");

    CASE("UT-MP-006: 缺少起始 boundary");
    RUN_ISOLATED("missing opening boundary", test_missing_opening_boundary);
    cleanup_malformed_case("UT-MP-006");

    CASE("UT-MP-007: 缺少结束 boundary");
    RUN_ISOLATED("missing closing boundary", test_missing_closing_boundary);
    cleanup_malformed_case("UT-MP-007");

    CASE("UT-MP-008: 文件段缺少 Content-Disposition");
    RUN_ISOLATED("missing file disposition", test_missing_file_disposition);
    cleanup_malformed_case("UT-MP-008");

    CASE("UT-MP-009: 文件段缺少 filename");
    RUN_ISOLATED("missing filename", test_missing_filename);
    cleanup_malformed_case("UT-MP-009");

    CASE("UT-MP-010: 缺少 user 字段");
    RUN_ISOLATED("missing user", test_missing_user);
    cleanup_malformed_case("UT-MP-010");

    CASE("UT-MP-011: 缺少 md5 字段");
    RUN_ISOLATED("missing md5", test_missing_md5);
    cleanup_malformed_case("UT-MP-011");

    CASE("UT-MP-012: 缺少 size 字段");
    RUN_ISOLATED("missing size", test_missing_size);
    cleanup_malformed_case("UT-MP-012");

    CASE("UT-MP-013: 声明大小小于实际内容");
    RUN_ISOLATED("declared size smaller", test_declared_size_smaller_than_content);
    cleanup_malformed_case("UT-MP-013");

    CASE("UT-MP-014: 声明大小大于实际内容");
    RUN_ISOLATED("declared size larger", test_declared_size_larger_than_content);
    cleanup_malformed_case("UT-MP-014");

    CASE("UT-MP-015: 非法 size 文本");
    RUN_ISOLATED("negative size", test_negative_size);
    cleanup_malformed_case("UT-MP-015a");
    RUN_ISOLATED("non-numeric size", test_non_numeric_size);
    cleanup_malformed_case("UT-MP-015b");
    RUN_ISOLATED("overflow size", test_overflow_size);
    cleanup_malformed_case("UT-MP-015c");

    CASE("UT-MP-016: 超长文件名");
    RUN_ISOLATED("overlong filename", test_overlong_filename);
    cleanup_malformed_case("UT-MP-016");

    CASE("UT-MP-017: 父目录路径文件名");
    RUN_ISOLATED("parent directory filename", test_parent_directory_filename);
    unlink("/tmp/escaped_by_upload_test.txt");
    cleanup_malformed_case("UT-MP-017");

    CASE("UT-MP-018: 文件内容包含 boundary 字节");
    RUN_ISOLATED("boundary bytes inside content", test_boundary_bytes_inside_content);
    cleanup_case("UT-MP-018");

    CASE("UT-MP-019: 元数据字段顺序变化");
    RUN_ISOLATED("reordered metadata fields", test_reordered_metadata_fields);
    cleanup_malformed_case("UT-MP-019");

    CASE("UT-MP-020: 报文在不同位置被截断");
    RUN_ISOLATED("truncated after opening boundary",
                 test_truncated_after_opening_boundary);
    cleanup_malformed_case("UT-MP-020a");
    RUN_ISOLATED("truncated in file content", test_truncated_in_file_content);
    cleanup_malformed_case("UT-MP-020b");
    RUN_ISOLATED("truncated in size value", test_truncated_in_size_value);
    cleanup_malformed_case("UT-MP-020c");

    CASE("UT-MP-021: 超长 boundary 行");
    RUN_ISOLATED("overlong boundary line", test_overlong_boundary_line);
    cleanup_malformed_case("UT-MP-021");

    CASE("UT-MP-022: 超长 user 值");
    RUN_ISOLATED("overlong user value", test_overlong_user_value);
    cleanup_malformed_case("UT-MP-022");

    CASE("UT-MP-023: 超长 md5 值");
    RUN_ISOLATED("overlong md5 value", test_overlong_md5_value);
    cleanup_malformed_case("UT-MP-023");

    CASE("UT-MP-024: 超长 size 文本");
    RUN_ISOLATED("overlong size text", test_overlong_size_text);
    cleanup_malformed_case("UT-MP-024");

    SUMMARY();
}
