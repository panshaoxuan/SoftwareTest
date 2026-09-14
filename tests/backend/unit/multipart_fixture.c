#include "multipart_fixture.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

long multipart_write_body(const char *path,
                          const unsigned char *content,
                          size_t content_size,
                          const char *user,
                          const char *md5,
                          const char *filename,
                          long declared_size,
                          const char *boundary)
{
    const char *delimiter = boundary ? boundary : TEST_MULTIPART_BOUNDARY;
    FILE *file = fopen(path, "wb");
    long body_size;

    if (!file || (!content && content_size > 0) || !user || !md5 || !filename) {
        if (file) fclose(file);
        return -1;
    }

    fprintf(file, "%s\r\n", delimiter);
    fprintf(file,
            "Content-Disposition: form-data; name=\"file\"; filename=\"%s\"\r\n",
            filename);
    fprintf(file, "Content-Type: application/octet-stream\r\n");
    fprintf(file, "\r\n");
    if (content_size > 0 && fwrite(content, 1, content_size, file) != content_size) {
        fclose(file);
        return -1;
    }
    fprintf(file, "\r\n");

    fprintf(file, "%s\r\n", delimiter);
    fprintf(file, "Content-Disposition: form-data; name=\"user\"\r\n\r\n");
    fprintf(file, "%s\r\n", user);

    fprintf(file, "%s\r\n", delimiter);
    fprintf(file, "Content-Disposition: form-data; name=\"md5\"\r\n\r\n");
    fprintf(file, "%s\r\n", md5);

    fprintf(file, "%s\r\n", delimiter);
    fprintf(file, "Content-Disposition: form-data; name=\"size\"\r\n\r\n");
    fprintf(file, "%ld\r\n", declared_size);
    fprintf(file, "%s--\r\n", delimiter);

    body_size = ftell(file);
    if (fclose(file) != 0) return -1;
    return body_size;
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
