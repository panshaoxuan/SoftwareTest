APP    = /app
CC     = gcc
INC    = -I$(APP)/include -I/usr/include/fastdfs -I/usr/include/fastcommon \
         -I/usr/local/include/hiredis -I/usr/include/mysql
CFLAGS = -Wall -g -O0 -fno-omit-frame-pointer

# 关键: 把 upload_cgi.c 的 main 改名, 才能链接我们自己的 main
UPLOAD_SRC = $(APP)/src_cgi/upload_cgi.c

WRAP = -Wl,--wrap=msql_conn \
       -Wl,--wrap=process_result_one \
       -Wl,--wrap=mysql_query \
       -Wl,--wrap=mysql_error \
       -Wl,--wrap=mysql_close

LIBS = -lhiredis -lfcgi -lmysqlclient -lfdfsclient -lfastcommon -lm -lpthread


TARGET = run_tests
OBJS   = upload_cgi_t.o test_upload_cgi.o mocks.o \
         $(APP)/common/make_log.o \
         $(APP)/common/util_cgi.o \
         $(APP)/common/cJSON.o   \
         $(APP)/common/redis_op.o \
         $(APP)/common/cfg.o

all: $(TARGET)

upload_cgi_t.o: $(UPLOAD_SRC)
	$(CC) $(CFLAGS) $(INC) -Dmain=upload_cgi_main -c $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) $(INC) -c $< -o $@

$(TARGET): $(OBJS)
	$(CC) $^ -o $@ $(WRAP) $(LIBS)

clean:
	rm -f *.o $(TARGET) /tmp/mt_child_rc /tmp/mt_child_kv
	rm -rf fixtures sandbox escaped_by_upload.txt

.PHONY: all clean
