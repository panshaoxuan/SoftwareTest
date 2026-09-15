/*
 * 阶段 5 分析工具（探针，不属于回归用例集）。
 *
 * 被测源码 upload_cgi.c 包含 fcgi_stdio.h，而该头文件做了两组替换：
 *     #define stdin  FCGI_stdin      -- 展开为 (&_fcgi_sF[0])
 *     #define fread  FCGI_fread
 *     #define printf FCGI_printf
 * 因此 recv_save_file() 里的
 *     int ret2 = fread(file_buf, 1, len, stdin);
 * 实际调用的是
 *     int ret2 = FCGI_fread(file_buf, 1, len, &_fcgi_sF[0]);
 * 也就是要经过 libfcgi 的 FCGI_FILE 层读取。
 *
 * 本工具观测两点：
 *   1. libfcgi 有没有把 _fcgi_sF[0].stdio_stream 指向真正的 stdin；
 *   2. FCGI_fread() 在这种情况下到底返回什么、有没有真正读走重定向后的报文。
 *
 * 业务源码不做任何修改，本文件只做观测。
 * 注意一：fcgi_stdio.h 把 printf 换成 FCGI_printf（写 _fcgi_sF[1]，未初始化时输出丢失），
 *         所以本探针一律用 snprintf + write(fd) 直接写标准输出。
 * 注意二：stdin 也是宏，因此在包含 fcgi_stdio.h 之前用一个函数把真正的 libc stdin 抓出来。
 */
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "multipart_fixture.h"

/*
 * fcgi_stdio.h 会把 stdin 定义成 FCGI_stdin、把 FILE 定义成 FCGI_FILE，
 * 所以在包含它之前先把真正的 libc 类型和 stdin 固定下来。
 */
typedef FILE *libc_file_ptr;

static libc_file_ptr real_libc_stdin(void)
{
    return stdin;
}

/* clearerr 同样被 fcgi_stdio.h 替换，这里在替换之前固定真正的实现。 */
static void real_clearerr(libc_file_ptr file)
{
    clearerr(file);
}

#include "fcgi_stdio.h"

#define PROBE_FILE    "probe_request.bin"
#define PROBE_SANDBOX "/tmp/upload_cgi_probe"

static void emit(const char *fmt, ...)
{
    char line[512];
    va_list ap;
    int n;
    ssize_t ignored;

    va_start(ap, fmt);
    n = vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    if (n <= 0) return;
    if (n > (int)sizeof(line) - 1) n = (int)sizeof(line) - 1;
    ignored = write(STDOUT_FILENO, line, (size_t)n);
    (void)ignored;
}

int main(void)
{
    static const unsigned char content[] = "hello yuncunchu\n";
    char probe[64];
    size_t got;
    long body_size;
    libc_file_ptr saved_stdin = real_libc_stdin();

    /* 先回收上一次可能残留的沙箱内容，再重建 */
    unlink(PROBE_SANDBOX "/" PROBE_FILE);
    rmdir(PROBE_SANDBOX);
    if (mkdir(PROBE_SANDBOX, 0700) != 0 || chdir(PROBE_SANDBOX) != 0) {
        emit("sandbox setup failed\n");
        return 90;
    }

    body_size = multipart_write_body(PROBE_FILE,
                                     content,
                                     sizeof(content) - 1,
                                     "alice",
                                     "3490f59b2b071332631aaa8e65cbd8f1",
                                     "probe.txt",
                                     (long)(sizeof(content) - 1),
                                     NULL);
    if (body_size < 0) {
        emit("multipart_write_body failed\n");
        return 91;
    }

    emit("real libc stdin          = %p\n", (void *)saved_stdin);
    emit("_fcgi_sF[0].stdio_stream = %p\n", (void *)_fcgi_sF[0].stdio_stream);
    emit("_fcgi_sF[0].fcgx_stream  = %p\n", (void *)_fcgi_sF[0].fcgx_stream);
    emit("FCGI_stdin               = %p\n", (void *)FCGI_stdin);
    emit("body_size                = %ld\n", body_size);

    /* --- 第一次：刻意不绑定 libfcgi 包装，复原夹具修复前的状态 --- */
    {
        int fd = open(PROBE_FILE, O_RDONLY);
        if (fd < 0 || dup2(fd, STDIN_FILENO) < 0) {
            emit("dup2 failed\n");
            return 92;
        }
        close(fd);
        real_clearerr(saved_stdin);
        _fcgi_sF[0].stdio_stream = NULL;
        memset(probe, 0, sizeof(probe));
        got = fread(probe, 1, sizeof(probe) - 1, stdin);
        emit("[修复前] stdio_stream    = %p\n", (void *)_fcgi_sF[0].stdio_stream);
        emit("[修复前] FCGI_fread      = %zu (SIZE_MAX=%zu)\n", got, (size_t)-1);
        emit("[修复前] first bytes     = [%.40s]\n", probe);
    }

    /* --- 第二次：走夹具的修复路径（multipart_redirect_stdin 会自动绑定） --- */
    if (multipart_redirect_stdin(PROBE_FILE) != 0) {
        emit("re-redirect failed\n");
        return 93;
    }
    emit("[修复后] stdio_stream    = %p\n", (void *)_fcgi_sF[0].stdio_stream);
    memset(probe, 0, sizeof(probe));
    got = fread(probe, 1, sizeof(probe) - 1, stdin);
    emit("[修复后] FCGI_fread      = %zu\n", got);
    emit("[修复后] first bytes     = [%.40s]\n", probe);

    unlink(PROBE_FILE);
    if (chdir("/tmp") == 0) rmdir(PROBE_SANDBOX);
    return 0;
}
