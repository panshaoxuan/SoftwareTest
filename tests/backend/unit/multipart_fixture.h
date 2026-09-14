#ifndef MULTIPART_FIXTURE_H
#define MULTIPART_FIXTURE_H

#include <stddef.h>

#define TEST_MULTIPART_BOUNDARY "------UploadUnitBoundary7MA4YWxk"

/* 按后端当前约定的 file -> user -> md5 -> size 顺序生成 multipart 报文。 */
long multipart_write_body(const char *path,
                          const unsigned char *content,
                          size_t content_size,
                          const char *user,
                          const char *md5,
                          const char *filename,
                          long declared_size,
                          const char *boundary);

/* 将指定报文文件重定向到标准输入，供 recv_save_file() 读取。 */
int multipart_redirect_stdin(const char *path);

/* 按二进制方式比较磁盘文件与期望内容。相同返回 1，否则返回 0。 */
int multipart_file_equals(const char *path,
                          const unsigned char *expected,
                          size_t expected_size);

#endif
