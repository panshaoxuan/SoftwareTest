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

# 阶段 5 分析工具（观测 recv_save_file 的可观测行为，不是回归用例）
ANALYZE_TARGET := analyze_recv_save_file
SANITIZE_ANALYZE_TARGET := analyze_recv_save_file_sanitize
PROBE_TARGET := probe_fcgi_stdin
SANITIZE_PROBE_TARGET := probe_fcgi_stdin_sanitize

TARGETS := $(BUFFER_TARGET) $(MULTIPART_TARGET)
SANITIZE_TARGETS := $(SANITIZE_BUFFER_TARGET) $(SANITIZE_MULTIPART_TARGET)
ANALYSIS_TARGETS := $(ANALYZE_TARGET) $(PROBE_TARGET)

.PHONY: all test test-buffer test-multipart \
        sanitize test-sanitize test-buffer-sanitize test-multipart-sanitize \
        analyze analyze-sanitize probe probe-sanitize clean

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

# ---------- 阶段 5 分析工具 ----------
# 只用于观测行为，不参与 make test / make test-sanitize 的通过判定。

$(ANALYZE_TARGET): analyze_recv_save_file.c multipart_fixture.c \
		$(UPLOAD_OBJECT) multipart_fixture.h
	$(CC) $(CPPFLAGS) $(CFLAGS) analyze_recv_save_file.c multipart_fixture.c \
		$(UPLOAD_OBJECT) $(LDFLAGS) $(LDLIBS) -o $@

$(SANITIZE_ANALYZE_TARGET): analyze_recv_save_file.c multipart_fixture.c \
		$(SANITIZE_UPLOAD_OBJECT) multipart_fixture.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SANITIZE_FLAGS) \
		analyze_recv_save_file.c multipart_fixture.c \
		$(SANITIZE_UPLOAD_OBJECT) $(LDFLAGS) $(LDLIBS) -o $@

analyze: $(ANALYZE_TARGET)
	./$(ANALYZE_TARGET)

analyze-sanitize: $(SANITIZE_ANALYZE_TARGET)
	./$(SANITIZE_ANALYZE_TARGET)

# 探针：观测 libfcgi 包装层是否真的把重定向后的标准输入交给被测函数
$(PROBE_TARGET): probe_fcgi_stdin.c multipart_fixture.c multipart_fixture.h
	$(CC) $(CPPFLAGS) $(CFLAGS) probe_fcgi_stdin.c multipart_fixture.c \
		$(LDFLAGS) $(LDLIBS) -o $@

$(SANITIZE_PROBE_TARGET): probe_fcgi_stdin.c multipart_fixture.c multipart_fixture.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SANITIZE_FLAGS) \
		probe_fcgi_stdin.c multipart_fixture.c \
		$(LDFLAGS) $(LDLIBS) -o $@

probe: $(PROBE_TARGET)
	./$(PROBE_TARGET)

probe-sanitize: $(SANITIZE_PROBE_TARGET)
	./$(SANITIZE_PROBE_TARGET)

clean:
	rm -f $(TARGETS) $(SANITIZE_TARGETS) $(ANALYSIS_TARGETS) \
		$(SANITIZE_ANALYZE_TARGET) $(SANITIZE_PROBE_TARGET) \
		$(UPLOAD_OBJECT) $(SANITIZE_UPLOAD_OBJECT)
