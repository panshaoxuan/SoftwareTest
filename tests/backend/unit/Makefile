CC := gcc
PROJECT_ROOT := ../../..

CPPFLAGS := -include sys/ipc.h -include ctype.h \
            -I$(PROJECT_ROOT)/include -I/usr/include/fastdfs \
            -I/usr/include/fastcommon -I/usr/local/include/hiredis \
            -I/usr/include/mysql -I/usr/local/include
CFLAGS := -Wall -Wextra -std=gnu11 -ffunction-sections -fdata-sections
LDFLAGS := -Wl,--gc-sections
LDLIBS := -lfcgi

# Sanitizer 构建与普通构建完全分离：独立 object、独立二进制，
# 避免检查器插桩污染日常回归使用的测试程序。
SANITIZE_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer -g

BUFFER_TARGET := test_buffer_search
MULTIPART_TARGET := test_recv_save_file
UPLOAD_OBJECT := upload_cgi_testable.o

SANITIZE_BUFFER_TARGET := test_buffer_search_sanitize
SANITIZE_MULTIPART_TARGET := test_recv_save_file_sanitize
SANITIZE_UPLOAD_OBJECT := upload_cgi_testable_sanitize.o

TARGETS := $(BUFFER_TARGET) $(MULTIPART_TARGET)
SANITIZE_TARGETS := $(SANITIZE_BUFFER_TARGET) $(SANITIZE_MULTIPART_TARGET)

.PHONY: all test test-buffer test-multipart \
        sanitize test-sanitize test-buffer-sanitize test-multipart-sanitize clean

all: $(TARGETS)

# ---------- 普通构建 ----------

$(UPLOAD_OBJECT): $(PROJECT_ROOT)/src_cgi/upload_cgi.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -D_LOG -Dmain=upload_cgi_main -c $< -o $@

$(BUFFER_TARGET): test_buffer_search.c $(UPLOAD_OBJECT) mini_test.h
	$(CC) $(CPPFLAGS) $(CFLAGS) test_buffer_search.c $(UPLOAD_OBJECT) \
		$(LDFLAGS) -o $@

$(MULTIPART_TARGET): test_recv_save_file.c multipart_fixture.c \
		$(UPLOAD_OBJECT) mini_test.h multipart_fixture.h
	$(CC) $(CPPFLAGS) $(CFLAGS) test_recv_save_file.c multipart_fixture.c \
		$(UPLOAD_OBJECT) $(LDFLAGS) $(LDLIBS) -o $@

test-buffer: $(BUFFER_TARGET)
	./$(BUFFER_TARGET)

test-multipart: $(MULTIPART_TARGET)
	./$(MULTIPART_TARGET)

test: test-buffer test-multipart

# ---------- Sanitizer 构建 ----------
# 只增加编译选项，不修改 src_cgi/ 下的业务源码。

sanitize: $(SANITIZE_TARGETS)

$(SANITIZE_UPLOAD_OBJECT): $(PROJECT_ROOT)/src_cgi/upload_cgi.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SANITIZE_FLAGS) \
		-D_LOG -Dmain=upload_cgi_main -c $< -o $@

$(SANITIZE_BUFFER_TARGET): test_buffer_search.c $(SANITIZE_UPLOAD_OBJECT) mini_test.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SANITIZE_FLAGS) \
		test_buffer_search.c $(SANITIZE_UPLOAD_OBJECT) \
		$(LDFLAGS) -o $@

$(SANITIZE_MULTIPART_TARGET): test_recv_save_file.c multipart_fixture.c \
		$(SANITIZE_UPLOAD_OBJECT) mini_test.h multipart_fixture.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SANITIZE_FLAGS) \
		test_recv_save_file.c multipart_fixture.c \
		$(SANITIZE_UPLOAD_OBJECT) $(LDFLAGS) $(LDLIBS) -o $@

# 用例在子进程中运行且可能被信号终止，Sanitizer 报告写入 stderr。
# 运行时应重定向输出保存为证据文件。
test-buffer-sanitize: $(SANITIZE_BUFFER_TARGET)
	./$(SANITIZE_BUFFER_TARGET)

test-multipart-sanitize: $(SANITIZE_MULTIPART_TARGET)
	./$(SANITIZE_MULTIPART_TARGET)

test-sanitize: test-buffer-sanitize test-multipart-sanitize

clean:
	rm -f $(TARGETS) $(SANITIZE_TARGETS) $(UPLOAD_OBJECT) $(SANITIZE_UPLOAD_OBJECT)
