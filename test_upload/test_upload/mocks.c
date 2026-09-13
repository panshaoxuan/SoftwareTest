/* MySQL 桩: 由 -Wl,--wrap= 在链接期替换真实实现。
 * 注意 msql_conn 返回假句柄 0x1, 因此 mysql_query/mysql_close 也必须被 wrap。 */
#include <stdio.h>
#include <string.h>
#include <mysql/mysql.h>
#include "mocks.h"

int  mock_conn_fail = 0;
int  mock_query_fail = 0;
int  mock_result_ret = 1;
char mock_result_val[512] = {0};
int  mock_result_queue[MOCK_QUEUE_MAX] = {0};
int  mock_result_queue_len = 0;
int  mock_result_queue_pos = 0;
char mock_sql_log[MOCK_SQL_SLOTS][MOCK_SQL_LEN];
int  mock_sql_count = 0;

static void record_sql(const char *sql)
{
    if (!sql) return;
    snprintf(mock_sql_log[mock_sql_count % MOCK_SQL_SLOTS], MOCK_SQL_LEN, "%s", sql);
    mock_sql_count++;
}

void mock_reset(void)
{
    mock_conn_fail = 0; mock_query_fail = 0; mock_result_ret = 1;
    memset(mock_result_val, 0, sizeof(mock_result_val));
    memset(mock_result_queue, 0, sizeof(mock_result_queue));
    mock_result_queue_len = 0; mock_result_queue_pos = 0;
    memset(mock_sql_log, 0, sizeof(mock_sql_log));
    mock_sql_count = 0;
}

int mock_sql_seen(const char *needle)
{
    int n = mock_sql_count < MOCK_SQL_SLOTS ? mock_sql_count : MOCK_SQL_SLOTS;
    for (int i = 0; i < n; i++)
        if (strstr(mock_sql_log[i], needle)) return 1;
    return 0;
}

MYSQL *__wrap_msql_conn(char *u, char *p, char *d)
{
    (void)u; (void)p; (void)d;
    if (mock_conn_fail) return NULL;
    return (MYSQL *)0x1;            /* 假句柄, 所有 mysql_* 都已被 wrap */
}

int __wrap_process_result_one(MYSQL *conn, char *sql, char *buf)
{
    (void)conn;
    record_sql(sql);
    int r = mock_result_ret;
    if (mock_result_queue_pos < mock_result_queue_len)
        r = mock_result_queue[mock_result_queue_pos++];
    if (buf) {
        if (r == 0) snprintf(buf, 512, "%s", mock_result_val);
        else buf[0] = '\0';
    }
    return r;
}

int __wrap_mysql_query(MYSQL *conn, const char *sql)
{
    (void)conn;
    record_sql(sql);
    return mock_query_fail ? 1 : 0;
}

const char *__wrap_mysql_error(MYSQL *conn) { (void)conn; return "mock error"; }
void __wrap_mysql_close(MYSQL *conn) { (void)conn; }
