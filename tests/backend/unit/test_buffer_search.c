#include <stddef.h>

#include "mini_test.h"

/* 被测函数来自 src_cgi/upload_cgi.c。 */
char *buffer_search(char *buf, int total_len, const char *sep, int seplen);

/* UT-BS-001：验证分隔符位于缓冲区开头时，返回缓冲区首地址。 */
static void test_separator_at_beginning(void)
{
    char input[] = "--boundary-body";
    char separator[] = "--boundary";

    CHECK(buffer_search(input, (int)strlen(input), separator,
                        (int)strlen(separator)) == input);
}

/* UT-BS-002：验证分隔符位于缓冲区中间时，返回其准确位置。 */
static void test_separator_in_middle(void)
{
    char input[] = "header\r\n\r\ncontent";
    char separator[] = "\r\n\r\n";
    char *result = buffer_search(input, (int)strlen(input), separator,
                                 (int)strlen(separator));

    CHECK(result == input + 6);
}

/* UT-BS-003：验证分隔符恰好位于缓冲区末尾时仍能被找到。 */
static void test_separator_at_end(void)
{
    char input[] = "abcXYZ";
    char separator[] = "XYZ";
    char *result = buffer_search(input, (int)strlen(input), separator,
                                 (int)strlen(separator));

    CHECK(result == input + 3);
}

/* UT-BS-004：验证不存在分隔符时返回 NULL，而不是越界地址。 */
static void test_separator_not_found(void)
{
    char input[] = "multipart-body";
    char separator[] = "missing";

    CHECK(buffer_search(input, (int)strlen(input), separator,
                        (int)strlen(separator)) == NULL);
}

/* UT-BS-005：验证缓冲区短于分隔符时安全返回 NULL。 */
static void test_buffer_shorter_than_separator(void)
{
    char input[] = "ab";
    char separator[] = "abc";

    CHECK(buffer_search(input, 2, separator, 3) == NULL);
}

/* UT-BS-006：验证函数按长度搜索二进制数据，不会被中间的 \0 截断。 */
static void test_binary_data_with_zero_byte(void)
{
    char input[] = {'a', '\0', 'b', 'X', 'Y'};
    char separator[] = {'b', 'X'};
    char *result = buffer_search(input, 5, separator, 2);

    CHECK(result == input + 2);
}

int main(void)
{
    CASE("UT-BS-001: 分隔符位于开头");
    test_separator_at_beginning();

    CASE("UT-BS-002: 分隔符位于中间");
    test_separator_in_middle();

    CASE("UT-BS-003: 分隔符位于末尾");
    test_separator_at_end();

    CASE("UT-BS-004: 分隔符不存在");
    test_separator_not_found();

    CASE("UT-BS-005: 缓冲区短于分隔符");
    test_buffer_shorter_than_separator();

    CASE("UT-BS-006: 二进制数据包含零字节");
    test_binary_data_with_zero_byte();

    SUMMARY();
}
