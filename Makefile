CC=gcc
CFLAGS=-Wall -Wextra -std=c99
TARGET=edit
SOURCES=edit.c term.c buffer.c ui.c files.c undo.c clipboard.c search.c utf8.c menu.c options.c
HEADERS=editor.h term.h buffer.h ui.h files.h undo.h clipboard.h search.h utf8.h menu.h options.h
TEST=test_utf8

$(TARGET): $(SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) -o $(TARGET) $(SOURCES)

$(TEST): test_utf8.c utf8.c utf8.h
	$(CC) $(CFLAGS) -o $(TEST) test_utf8.c utf8.c

test: $(TEST)
	./$(TEST)

clean:
	del $(TARGET).exe $(TEST).exe

.PHONY: clean test
