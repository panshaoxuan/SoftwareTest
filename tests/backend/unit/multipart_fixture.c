#include "multipart_fixture.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

multipart_body_options multipart_default_options(void)
{
    const multipart_body_options options = {1, 1, 1, 1, 1, 1, 1, 1, NULL};
    return options;
}

long multipart_write_bytes(const char *path,
                           const unsigned char *data,
                           size_t data_size)
{
    FILE *file;

    if (!path || (!data && data_size > 0)) return -1;
    file = fopen(path, "wb");
    if (!file) return -1;
    if (data_size > 0 && fwrite(data, 1, data_size, file) != data_size) {
        fclose(file);
        return -1;
    }
    if (fclose(file) != 0) return -1;
    return (long)data_size;
}

long multipart_write_body_with_options(const char *path,
                                       const unsigned char *content,
                                       size_t content_size,
                                       const char *user,
                                       const char *md5,
                                       const char *filename,
                                       long declared_size,
                                       const char *boundary,
                                       const multipart_body_options *options)
{
    const char *delimiter = boundary ? boundary : TEST_MULTIPART_BOUNDARY;
    multipart_body_options defaults = multipart_default_options();
    const multipart_body_options *opts = options ? options : &defaults;
    FILE *file = fopen(path, "wb");
    long body_size;

    if (!file || (!content && content_size > 0) || !user || !md5 || !filename) {
        if (file) fclose(file);
        return -1;
    }

    if (opts->include_opening_boundary) fprintf(file, "%s\r\n", delimiter);
    if (opts->include_file_disposition) {
        fprintf(file, "Content-Disposition: form-data; name=\"file\"");
        if (opts->include_filename) fprintf(file, "; filename=\"%s\"", filename);
        fprintf(file, "\r\n");
    }
    if (opts->include_content_type)
        fprintf(file, "Content-Type: application/octet-stream\r\n");
    fprintf(file, "\r\n");
    if (content_size > 0 && fwrite(content, 1, content_size, file) != content_size) {
        fclose(file);
        return -1;
    }
    fprintf(file, "\r\n");

    if (opts->include_user) {
        fprintf(file, "%s\r\n", delimiter);
        fprintf(file, "Content-Disposition: form-data; name=\"user\"\r\n\r\n");
        fprintf(file, "%s\r\n", user);
    }

    if (opts->include_md5) {
        fprintf(file, "%s\r\n", delimiter);
        fprintf(file, "Content-Disposition: form-data; name=\"md5\"\r\n\r\n");
        fprintf(file, "%s\r\n", md5);
    }

    if (opts->include_size) {
        fprintf(file, "%s\r\n", delimiter);
        fprintf(file, "Content-Disposition: form-data; name=\"size\"\r\n\r\n");
        if (opts->size_text_override)
            fprintf(file, "%s\r\n", opts->size_text_override);
        else
            fprintf(file, "%ld\r\n", declared_size);
    }
    if (opts->include_closing_boundary) fprintf(file, "%s--\r\n", delimiter);

    body_size = ftell(file);
    if (fclose(file) != 0) return -1;
    return body_size;
}

long multipart_write_body(const char *path,
                          const unsigned char *content,
                          size_t content_size,
                          const char *user,
                          const char *md5,
                          const char *filename,
                          long declared_size,
                          const char *boundary)
{
    multipart_body_options options = multipart_default_options();
    return multipart_write_body_with_options(path,
                                             content,
                                             content_size,
                                             user,
                                             md5,
                                             filename,
                                             declared_size,
                                             boundary,
                                             &options);
}

/*
 * 与 fcgi_stdio.h 中 FCGI_FILE 的布局一致：两个指针。
 * 这里不直接包含 fcgi_stdio.h，因为它会把 fopen/fread/printf 全部宏替换掉，
 * 夹具自身的文件读写就会被 libfcgi 包装接管。
 */
typedef struct {
    FILE *stdio_stream;
    void *fcgx_stream;
} fixture_fcgi_file;

/* libfcgi 导出的 FCGI_FILE 数组，_fcgi_sF[0] 即 FCGI_stdin。 */
extern fixture_fcgi_file _fcgi_sF[];

void multipart_bind_fcgi_stdin(void)
{
    _fcgi_sF[0].stdio_stream = stdin;
}

int multipart_redirect_stdin(const char *path)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    if (dup2(fd, STDIN_FILENO) < 0) {
        close(fd);
        return -1;
    }
    close(fd);
    clearerr(stdin);
    /* 被测函数经 libfcgi 包装读取，必须同时把包装指向真正的 stdin。 */
    multipart_bind_fcgi_stdin();
    return 0;
}

int multipart_file_equals(const char *path,
                          const unsigned char *expected,
                          size_t expected_size)
{
    unsigned char buffer[4096];
    size_t offset = 0;
    FILE *file = fopen(path, "rb");
    if (!file) return 0;

    while (offset < expected_size) {
        size_t remaining = expected_size - offset;
        size_t chunk = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
        size_t read_count = fread(buffer, 1, chunk, file);
        if (read_count != chunk || memcmp(buffer, expected + offset, chunk) != 0) {
            fclose(file);
            return 0;
        }
        offset += read_count;
    }

    if (fgetc(file) != EOF) {
        fclose(file);
        return 0;
    }
    fclose(file);
    return 1;
}
