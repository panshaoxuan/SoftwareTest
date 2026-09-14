CC := gcc
PROJECT_ROOT := ../../..

CPPFLAGS := -include sys/ipc.h -include ctype.h \
            -I$(PROJECT_ROOT)/include -I/usr/include/fastdfs \
            -I/usr/include/fastcommon -I/usr/local/include/hiredis \
            -I/usr/include/mysql -I/usr/local/include
CFLAGS := -Wall -Wextra -std=gnu11 -ffunction-sections -fdata-sections
LDFLAGS := -Wl,--gc-sections
LDLIBS := -lfcgi

BUFFER_TARGET := test_buffer_search
MULTIPART_TARGET := test_recv_save_file
TARGETS := $(BUFFER_TARGET) $(MULTIPART_TARGET)
UPLOAD_OBJECT := upload_cgi_testable.o

.PHONY: all test test-buffer test-multipart clean

all: $(TARGETS)

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

clean:
	rm -f $(TARGETS) $(UPLOAD_OBJECT)
