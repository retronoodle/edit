CC=gcc
CFLAGS=-Wall -Wextra -std=c99
TARGET=edit
SOURCES=edit.c term.c buffer.c ui.c files.c undo.c
HEADERS=editor.h term.h buffer.h ui.h files.h undo.h

$(TARGET): $(SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) -o $(TARGET) $(SOURCES)

clean:
	del $(TARGET).exe

.PHONY: clean
