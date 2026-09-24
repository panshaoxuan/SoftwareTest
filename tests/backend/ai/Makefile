# AI 语义检索模块（模块二）C 单元测试
#
# 与 unit/ 下的上传模块测试遵循同一约定：直接编译仓库内的被测源码，
# 不复制函数实现，测试通过 mk 目标执行。
#
# 与 upload 测试的两处差异：
#   1. 被测源码是 C++（ai_cgi.cpp），故用 g++；
#   2. handle_describe/handle_search/handle_rebuild 是 static，且 main 被 FCGI_Accept()
#      循环包住无法驱动，因此 test_ai_cgi.cpp 直接 #include 被测源码（并把 main 改名），
#      本 Makefile 不单独编译 ai_cgi.o。
#
# 外部依赖（Redis token 校验、MySQL、DashScope、FAISS）在测试内打桩，
# 因此不链接 -lmysqlclient / -lhiredis / -lfaiss。

CXX := g++
CC  := gcc
PROJECT_ROOT := ../../..

CPPFLAGS := -I. -I../unit -I$(PROJECT_ROOT)/src_cgi -I$(PROJECT_ROOT)/include \
            -I/usr/include/fastdfs -I/usr/include/fastcommon \
            -I/usr/local/include/hiredis -I/usr/include/mysql -I/usr/local/include
CXXFLAGS := -Wall -std=c++17 -Wno-write-strings
CFLAGS   := -Wall -std=gnu11
LDFLAGS  :=
# -lfcgi 提供 _fcgi_sF[]（FCGI_FILE 数组），测试用它把 FCGI_stdout 绑到真 stdout；
# -lcurl 因为被测源码内仍有 curl_global_init / download_file 的引用（本测试不触发）。
LDLIBS   := -lfcgi -lcurl -lm -lpthread

TARGET := test_ai_cgi

# 被测源码需要的最小公共模块（其余依赖已被桩替代）
COMMON_SRCS := cJSON.c make_log.c cfg.c md5.c base64.c
COMMON_OBJS := $(COMMON_SRCS:.c=.o)

.PHONY: all test clean

all: $(TARGET)

$(TARGET): test_ai_cgi.cpp ../unit/mini_test.h $(COMMON_OBJS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) test_ai_cgi.cpp $(COMMON_OBJS) \
		$(LDFLAGS) -o $@ $(LDLIBS)

%.o: $(PROJECT_ROOT)/common/%.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) $(COMMON_OBJS)
