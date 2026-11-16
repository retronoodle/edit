CC=gcc
CFLAGS=-Wall -Wextra -std=c99
TARGET=velo
SRC=src
TESTDIR=tests
SOURCES=$(SRC)/clipcodec.c $(SRC)/velo.c $(SRC)/term.c $(SRC)/buffer.c $(SRC)/ui.c $(SRC)/files.c $(SRC)/undo.c $(SRC)/clipboard.c $(SRC)/search.c $(SRC)/utf8.c $(SRC)/menu.c $(SRC)/options.c $(SRC)/recovery.c $(SRC)/mouse.c $(SRC)/tabs.c $(SRC)/recent.c $(SRC)/syntax.c $(SRC)/gitinfo.c $(SRC)/gitmodal.c $(SRC)/watch.c $(SRC)/aisetup.c $(SRC)/ai.c $(SRC)/aiask.c $(SRC)/aiedit.c $(SRC)/aiexplain.c $(SRC)/viewer.c $(SRC)/fuzzy.c $(SRC)/quickopen.c $(SRC)/diffcap.c $(SRC)/gitpush.c
HEADERS=$(SRC)/clipcodec.h $(SRC)/editor.h $(SRC)/term.h $(SRC)/buffer.h $(SRC)/ui.h $(SRC)/files.h $(SRC)/undo.h $(SRC)/clipboard.h $(SRC)/search.h $(SRC)/utf8.h $(SRC)/menu.h $(SRC)/options.h $(SRC)/recovery.h $(SRC)/mouse.h $(SRC)/tabs.h $(SRC)/recent.h $(SRC)/syntax.h $(SRC)/gitinfo.h $(SRC)/gitmodal.h $(SRC)/watch.h $(SRC)/aisetup.h $(SRC)/ai.h $(SRC)/aiask.h $(SRC)/aiedit.h $(SRC)/aiexplain.h $(SRC)/viewer.h $(SRC)/fuzzy.h $(SRC)/quickopen.h $(SRC)/diffcap.h $(SRC)/gitpush.h
TEST=test_utf8
TEST_SYNTAX=test_syntax
TEST_OPTIONS=test_options
TEST_CLIPCODEC=test_clipcodec
TEST_AI=test_ai
TEST_FUZZY=test_fuzzy
TEST_DIFFCAP=test_diffcap
TEST_GITPUSH=test_gitpush

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

$(TEST_AI): $(TESTDIR)/test_ai.c $(SRC)/ai.c $(SRC)/ai.h $(SRC)/utf8.c $(SRC)/utf8.h
	$(CC) $(CFLAGS) -I$(SRC) -o $(TEST_AI) $(TESTDIR)/test_ai.c $(SRC)/ai.c $(SRC)/utf8.c

$(TEST_FUZZY): $(TESTDIR)/test_fuzzy.c $(SRC)/fuzzy.c $(SRC)/fuzzy.h
	$(CC) $(CFLAGS) -I$(SRC) -o $(TEST_FUZZY) $(TESTDIR)/test_fuzzy.c $(SRC)/fuzzy.c

$(TEST_DIFFCAP): $(TESTDIR)/test_diffcap.c $(SRC)/diffcap.c $(SRC)/diffcap.h
	$(CC) $(CFLAGS) -I$(SRC) -o $(TEST_DIFFCAP) $(TESTDIR)/test_diffcap.c $(SRC)/diffcap.c

$(TEST_GITPUSH): $(TESTDIR)/test_gitpush.c $(SRC)/gitpush.c $(SRC)/gitpush.h
	$(CC) $(CFLAGS) -I$(SRC) -o $(TEST_GITPUSH) $(TESTDIR)/test_gitpush.c $(SRC)/gitpush.c

test: $(TEST) $(TEST_SYNTAX) $(TEST_OPTIONS) $(TEST_CLIPCODEC) $(TEST_AI) $(TEST_FUZZY) $(TEST_DIFFCAP) $(TEST_GITPUSH)
	./$(TEST)
	./$(TEST_SYNTAX)
	./$(TEST_OPTIONS)
	./$(TEST_CLIPCODEC)
	./$(TEST_AI)
	./$(TEST_FUZZY)
	./$(TEST_DIFFCAP)
	./$(TEST_GITPUSH)

ifeq ($(OS),Windows_NT)
EXE=.exe
RM=del
else
EXE=
RM=rm -f
endif

clean:
	$(RM) $(TARGET)$(EXE) $(TEST)$(EXE) $(TEST_SYNTAX)$(EXE) $(TEST_OPTIONS)$(EXE) $(TEST_CLIPCODEC)$(EXE) $(TEST_AI)$(EXE) $(TEST_FUZZY)$(EXE) $(TEST_DIFFCAP)$(EXE) $(TEST_GITPUSH)$(EXE)

.PHONY: clean test
