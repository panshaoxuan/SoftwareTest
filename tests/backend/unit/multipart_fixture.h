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

/* 按二进制方式比较磁盘文件与期望内容。相同返回 1，否则返回 0。 */
int multipart_file_equals(const char *path,
                          const unsigned char *expected,
                          size_t expected_size);

#endif
