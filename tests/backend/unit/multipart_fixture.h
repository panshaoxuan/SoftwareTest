#ifndef MULTIPART_FIXTURE_H
#define MULTIPART_FIXTURE_H

#include <stddef.h>

#define TEST_MULTIPART_BOUNDARY "------UploadUnitBoundary7MA4YWxk"

typedef struct {
    int include_opening_boundary;
    int include_file_disposition;
    int include_filename;
    int include_content_type;
    int include_user;
    int include_md5;
    int include_size;
    int include_closing_boundary;
    const char *size_text_override;
} multipart_body_options;

/* 返回生成正常 multipart 报文所需的默认选项。 */
multipart_body_options multipart_default_options(void);

/* 按后端当前约定的 file -> user -> md5 -> size 顺序生成 multipart 报文。 */
long multipart_write_body(const char *path,
                          const unsigned char *content,
                          size_t content_size,
                          const char *user,
                          const char *md5,
                          const char *filename,
                          long declared_size,
                          const char *boundary);

/* 按选项生成结构缺失类报文；供异常解析测试复用。 */
long multipart_write_body_with_options(const char *path,
                                       const unsigned char *content,
                                       size_t content_size,
                                       const char *user,
                                       const char *md5,
                                       const char *filename,
                                       long declared_size,
                                       const char *boundary,
                                       const multipart_body_options *options);

/* 原样写入任意字节，用于构造截断等不能由结构化选项表达的报文。 */
long multipart_write_bytes(const char *path,
                           const unsigned char *data,
                           size_t data_size);

/* 将指定报文文件重定向到标准输入，供 recv_save_file() 读取。 */
int multipart_redirect_stdin(const char *path);

/*
 * 把 libfcgi 的 FCGI_stdin 包装接到真正的 stdin。
 *
 * 被测源码 upload_cgi.c 包含 fcgi_stdio.h，其中
 *     #define stdin  FCGI_stdin      （即 &_fcgi_sF[0]）
 *     #define fread  FCGI_fread
 * 使 recv_save_file() 里的 `fread(file_buf, 1, len, stdin)` 实际变成
 *     FCGI_fread(file_buf, 1, len, &_fcgi_sF[0])。
 * 测试进程从不调用 FCGI_Accept()，`_fcgi_sF[0].stdio_stream` 保持 NULL，
 * 于是 FCGI_fread() 直接返回 (size_t)-1，根本没有读取重定向后的标准输入；
 * recv_save_file() 又把它赋给 `int`，-1 不等于 0，错误分支不会触发，
 * 后续解析就作用在未初始化的 malloc 缓冲区上。
 *
 * 这里只补齐 libfcgi 包装层，属于测试夹具的输入通路修复，
 * 不改变被测源码的任何行为。multipart_redirect_stdin() 会自动调用它。
 */
void multipart_bind_fcgi_stdin(void);

/* 按二进制方式比较磁盘文件与期望内容。相同返回 1，否则返回 0。 */
int multipart_file_equals(const char *path,
                          const unsigned char *expected,
                          size_t expected_size);

#endif
