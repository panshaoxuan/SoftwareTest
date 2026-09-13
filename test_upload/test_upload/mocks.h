#ifndef MOCKS_H
#define MOCKS_H
#define MOCK_SQL_SLOTS 64
#define MOCK_SQL_LEN   1024
#define MOCK_QUEUE_MAX 16

extern int  mock_conn_fail;        /* 1: msql_conn 返回 NULL */
extern int  mock_query_fail;       /* 1: mysql_query 返回非 0 (模拟 DB 失败) */
extern int  mock_result_ret;       /* process_result_one 默认返回值 */
extern char mock_result_val[512];  /* 返回 0 时写入 buf 的值 */
extern int  mock_result_queue[MOCK_QUEUE_MAX];
extern int  mock_result_queue_len;
extern int  mock_result_queue_pos;
extern char mock_sql_log[MOCK_SQL_SLOTS][MOCK_SQL_LEN];
extern int  mock_sql_count;

void mock_reset(void);
int  mock_sql_seen(const char *needle);
#endif
