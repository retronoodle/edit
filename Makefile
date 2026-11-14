CC=gcc
CFLAGS=-Wall -Wextra -std=c99
TARGET=edit
SOURCES=edit.c term.c buffer.c ui.c files.c undo.c clipboard.c search.c utf8.c menu.c options.c recovery.c mouse.c tabs.c recent.c syntax.c gitinfo.c gitmodal.c
HEADERS=editor.h term.h buffer.h ui.h files.h undo.h clipboard.h search.h utf8.h menu.h options.h recovery.h mouse.h tabs.h recent.h syntax.h gitinfo.h gitmodal.h
TEST=test_utf8
TEST_SYNTAX=test_syntax

$(TARGET): $(SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) -o $(TARGET) $(SOURCES)

$(TEST): test_utf8.c utf8.c utf8.h
	$(CC) $(CFLAGS) -o $(TEST) test_utf8.c utf8.c

$(TEST_SYNTAX): test_syntax.c syntax.c syntax.h
	$(CC) $(CFLAGS) -o $(TEST_SYNTAX) test_syntax.c syntax.c

test: $(TEST) $(TEST_SYNTAX)
	./$(TEST)
	./$(TEST_SYNTAX)

clean:
	del $(TARGET).exe $(TEST).exe $(TEST_SYNTAX).exe

.PHONY: clean test
