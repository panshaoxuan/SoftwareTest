/*
 * 阶段 5 内存安全分析工具（分析用，不属于回归用例集）。
 *
 * 目的：在普通构建与 Sanitizer 构建之间对比 recv_save_file() 的可观测行为。
 * 直接调用真实函数，打印：
 *   - 返回值；
 *   - 标准输入实际被消耗的字节数（用于区分"fread 阶段就失败"与"解析中途失败"）；
 *   - 解析出的 user / filename / md5 / size；
 *   - 输出文件是否生成。
 *
 * 业务源码 src_cgi/upload_cgi.c 不做任何修改；本文件只做观测。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "multipart_fixture.h"
#include "util_cgi.h"

int recv_save_file(long len, char *user, char *filename, char *md5, long *p_size);

#define ANALYZE_SANDBOX "/tmp/upload_cgi_analyze"
#define ANALYZE_OUTPUT  "note.txt"
#define ANALYZE_USER    "alice"
#define ANALYZE_MD5     "3490f59b2b071332631aaa8e65cbd8f1"

int main(void)
{
    static const unsigned char content[] = "hello yuncunchu\n";
    char user[USER_NAME_LEN] = {0};
    char filename[FILE_NAME_LEN] = {0};
    char md5[MD5_LEN] = {0};
    long size = -1;
    long body_size;
    long consumed;
    off_t before;
    off_t after;
    int result;

    rmdir(ANALYZE_SANDBOX);
    if (mkdir(ANALYZE_SANDBOX, 0700) != 0 || chdir(ANALYZE_SANDBOX) != 0) {
        perror("sandbox");
        return 90;
    }

    body_size = multipart_write_body("request.bin",
                                     content,
                                     sizeof(content) - 1,
                                     ANALYZE_USER,
                                     ANALYZE_MD5,
                                     ANALYZE_OUTPUT,
                                     (long)(sizeof(content) - 1),
                                     NULL);
    if (body_size < 0) {
        fprintf(stderr, "multipart_write_body failed\n");
        return 91;
    }
    if (multipart_redirect_stdin("request.bin") != 0) {
        fprintf(stderr, "multipart_redirect_stdin failed\n");
        return 92;
    }

    before = lseek(STDIN_FILENO, 0, SEEK_CUR);
    result = recv_save_file(body_size, user, filename, md5, &size);
    after = lseek(STDIN_FILENO, 0, SEEK_CUR);
    consumed = (before < 0 || after < 0) ? -1 : (long)(after - before);

    printf("body_size      = %ld\n", body_size);
    printf("result         = %d\n", result);
    printf("stdin_consumed = %ld\n", consumed);
    printf("user           = [%s]\n", user);
    printf("filename       = [%s]\n", filename);
    printf("md5            = [%s]\n", md5);
    printf("size           = %ld\n", size);
    printf("output_exists  = %d\n", access(ANALYZE_OUTPUT, F_OK) == 0);

    unlink("request.bin");
    unlink(ANALYZE_OUTPUT);
    if (chdir("/tmp") == 0) rmdir(ANALYZE_SANDBOX);
    return 0;
}
