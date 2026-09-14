#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mini_test.h"
#include "multipart_fixture.h"

/* 被测函数来自 src_cgi/upload_cgi.c。 */
int recv_save_file(long len, char *user, char *filename, char *md5, long *p_size);

typedef struct {
    const unsigned char *content;
    size_t content_size;
    const char *filename;
    const char *user;
    const char *md5;
    long declared_size;
    int allow_explicit_rejection;
} valid_case;

/*
 * 每个用例都在独立子进程和临时目录中运行：
 * 1. 动态生成 multipart 报文；2. 重定向 stdin；3. 调用真实解析函数；
 * 4. 校验输出字段与落盘内容；5. 清理测试文件。
 */
static int run_valid_case(const valid_case *item)
{
    char sandbox_template[] = "/tmp/upload_cgi_multipart_XXXXXX";
    char *sandbox = mkdtemp(sandbox_template);
    char parsed_user[128] = {0};
    char parsed_filename[256] = {0};
    char parsed_md5[256] = {0};
    long parsed_size = -1;
    long body_size;
    int result;
    int rc = 0;

    if (!sandbox || chdir(sandbox) != 0) return 90;

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
    if (chdir("/tmp") == 0) rmdir(sandbox);
    return rc;
}

/* UT-MP-001 / TC-N-001：正常文本报文应正确解析四个字段并保存文件。 */
static int test_normal_text_multipart(void)
{
    static const unsigned char content[] = "hello yuncunchu\n";
    const valid_case item = {
        content, sizeof(content) - 1, "note.txt", "alice",
        "3490f59b2b071332631aaa8e65cbd8f1", (long)(sizeof(content) - 1), 0
    };
    return run_valid_case(&item);
}

/* UT-MP-002 / TC-N-002：二进制内容中的 \0 不应导致内容被截断。 */
static int test_binary_content_with_zero_bytes(void)
{
    static const unsigned char content[] = {0x00, 0x01, 'A', 0x00, 'B', 0xff};
    const valid_case item = {
        content, sizeof(content), "binary.dat", "alice",
        "72cdb1041b332428c1061394945ea8f7", (long)sizeof(content), 0
    };
    return run_valid_case(&item);
}

/* UT-MP-003 / TC-N-003：UTF-8 中文和空格文件名应保持原样。 */
static int test_chinese_and_space_filename(void)
{
    static const unsigned char content[] = "unicode filename";
    const valid_case item = {
        content, sizeof(content) - 1, "我的 文档-1.txt", "alice",
        "9d4ba1f1c835ec52ad3b445f0ab7c98f", (long)(sizeof(content) - 1), 0
    };
    return run_valid_case(&item);
}

/* UT-MP-004 / TC-B-001：需求未定，记录接受或明确拒绝行为，并确保不崩溃、不残留。 */
static int test_zero_byte_file_current_baseline(void)
{
    static const unsigned char empty_content[] = {0};
    const valid_case item = {
        empty_content, 0, "empty.bin", "alice",
        "d41d8cd98f00b204e9800998ecf8427e", 0, 1
    };
    return run_valid_case(&item);
}

/* UT-MP-005 / TC-S-005：声明 size 与实际字节数相同时，p_size 应精确匹配。 */
static int test_declared_size_matches_content(void)
{
    static const unsigned char content[] = "123456789";
    const valid_case item = {
        content, sizeof(content) - 1, "size-check.bin", "bob",
        "25f9e794323b453885f5181f1b624d0b", 9, 0
    };
    return run_valid_case(&item);
}

int main(void)
{
    CASE("UT-MP-001: 正常文本 multipart 报文");
    RUN_ISOLATED("normal text multipart", test_normal_text_multipart);

    CASE("UT-MP-002: 文件内容包含二进制零字节");
    RUN_ISOLATED("binary content", test_binary_content_with_zero_bytes);

    CASE("UT-MP-003: 中文和空格文件名");
    RUN_ISOLATED("unicode filename", test_chinese_and_space_filename);

    CASE("UT-MP-004: 0 字节文件当前行为基线");
    NOTE("当前需求未最终确认；本测试记录实际行为，并检查进程安全与文件残留");
    RUN_ISOLATED("zero-byte file", test_zero_byte_file_current_baseline);

    CASE("UT-MP-005: 声明大小与实际内容一致");
    RUN_ISOLATED("declared size matches", test_declared_size_matches_content);

    SUMMARY();
}
