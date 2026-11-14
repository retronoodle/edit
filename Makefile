CC=gcc
CFLAGS=-Wall -Wextra -std=c99
TARGET=edit
SOURCES=edit.c term.c buffer.c ui.c files.c undo.c clipboard.c search.c
HEADERS=editor.h term.h buffer.h ui.h files.h undo.h clipboard.h search.h

$(TARGET): $(SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) -o $(TARGET) $(SOURCES)

clean:
	del $(TARGET).exe

.PHONY: clean
