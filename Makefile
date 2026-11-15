CC=gcc
CFLAGS=-Wall -Wextra -std=c99
TARGET=velo
SRC=src
TESTDIR=tests
SOURCES=$(SRC)/clipcodec.c $(SRC)/velo.c $(SRC)/term.c $(SRC)/buffer.c $(SRC)/ui.c $(SRC)/files.c $(SRC)/undo.c $(SRC)/clipboard.c $(SRC)/search.c $(SRC)/utf8.c $(SRC)/menu.c $(SRC)/options.c $(SRC)/recovery.c $(SRC)/mouse.c $(SRC)/tabs.c $(SRC)/recent.c $(SRC)/syntax.c $(SRC)/gitinfo.c $(SRC)/gitmodal.c $(SRC)/watch.c $(SRC)/aisetup.c
HEADERS=$(SRC)/clipcodec.h $(SRC)/editor.h $(SRC)/term.h $(SRC)/buffer.h $(SRC)/ui.h $(SRC)/files.h $(SRC)/undo.h $(SRC)/clipboard.h $(SRC)/search.h $(SRC)/utf8.h $(SRC)/menu.h $(SRC)/options.h $(SRC)/recovery.h $(SRC)/mouse.h $(SRC)/tabs.h $(SRC)/recent.h $(SRC)/syntax.h $(SRC)/gitinfo.h $(SRC)/gitmodal.h $(SRC)/watch.h $(SRC)/aisetup.h
TEST=test_utf8
TEST_SYNTAX=test_syntax
TEST_OPTIONS=test_options
TEST_CLIPCODEC=test_clipcodec

$(TARGET): $(SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) -I$(SRC) -o $(TARGET) $(SOURCES)

$(TEST): $(TESTDIR)/test_utf8.c $(SRC)/utf8.c $(SRC)/utf8.h
	$(CC) $(CFLAGS) -I$(SRC) -o $(TEST) $(TESTDIR)/test_utf8.c $(SRC)/utf8.c

$(TEST_SYNTAX): $(TESTDIR)/test_syntax.c $(SRC)/syntax.c $(SRC)/syntax.h
	$(CC) $(CFLAGS) -I$(SRC) -o $(TEST_SYNTAX) $(TESTDIR)/test_syntax.c $(SRC)/syntax.c

$(TEST_OPTIONS): $(TESTDIR)/test_options.c $(SRC)/options.c $(SRC)/options.h $(SRC)/recent.c $(SRC)/recent.h $(SRC)/utf8.c $(SRC)/utf8.h
	$(CC) $(CFLAGS) -I$(SRC) -o $(TEST_OPTIONS) $(TESTDIR)/test_options.c $(SRC)/options.c $(SRC)/recent.c $(SRC)/utf8.c

$(TEST_CLIPCODEC): $(TESTDIR)/test_clipcodec.c $(SRC)/clipcodec.c $(SRC)/clipcodec.h
	$(CC) $(CFLAGS) -I$(SRC) -o $(TEST_CLIPCODEC) $(TESTDIR)/test_clipcodec.c $(SRC)/clipcodec.c

test: $(TEST) $(TEST_SYNTAX) $(TEST_OPTIONS) $(TEST_CLIPCODEC)
	./$(TEST)
	./$(TEST_SYNTAX)
	./$(TEST_OPTIONS)
	./$(TEST_CLIPCODEC)

ifeq ($(OS),Windows_NT)
EXE=.exe
RM=del
else
EXE=
RM=rm -f
endif

clean:
	$(RM) $(TARGET)$(EXE) $(TEST)$(EXE) $(TEST_SYNTAX)$(EXE) $(TEST_OPTIONS)$(EXE) $(TEST_CLIPCODEC)$(EXE)

.PHONY: clean test
