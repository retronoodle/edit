CC=gcc
CFLAGS=-Wall -Wextra -std=c99
TARGET=edit
SOURCE=edit.c

$(TARGET): $(SOURCE)
	$(CC) $(CFLAGS) -o $(TARGET) $(SOURCE)

clean:
	del $(TARGET).exe

.PHONY: clean