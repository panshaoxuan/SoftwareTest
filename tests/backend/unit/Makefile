CC := gcc
PROJECT_ROOT := ../../..

CPPFLAGS := -include sys/ipc.h -include ctype.h \
            -I$(PROJECT_ROOT)/include -I/usr/include/fastdfs \
            -I/usr/include/fastcommon -I/usr/local/include/hiredis \
            -I/usr/include/mysql -I/usr/local/include
CFLAGS := -Wall -Wextra -std=gnu11 -ffunction-sections -fdata-sections
LDFLAGS := -Wl,--gc-sections

TARGET := test_buffer_search
UPLOAD_OBJECT := upload_cgi_testable.o

.PHONY: all test clean

all: $(TARGET)

$(UPLOAD_OBJECT): $(PROJECT_ROOT)/src_cgi/upload_cgi.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -Dmain=upload_cgi_main -c $< -o $@

$(TARGET): test_buffer_search.c $(UPLOAD_OBJECT) mini_test.h
	$(CC) $(CPPFLAGS) $(CFLAGS) test_buffer_search.c $(UPLOAD_OBJECT) \
		$(LDFLAGS) -o $@

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) $(UPLOAD_OBJECT)
