CC = gcc
CFLAGS = -O2 -lm -pthread -lssl -lcrypto
FLEX = flex
BISON = bison

SRC_DIR = src
TOOLS_DIR = tools/gdu
BIN_DIR = bin

GLOWLANG_BIN = $(BIN_DIR)/glowlang
GDU_BIN = $(BIN_DIR)/gdu

all: $(GLOWLANG_BIN) $(GDU_BIN)

$(GLOWLANG_BIN): $(SRC_DIR)/glowlang.l $(SRC_DIR)/glowlang.y $(SRC_DIR)/lsp.c $(SRC_DIR)/glowlang.h $(SRC_DIR)/lsp.h $(SRC_DIR)/arena.c $(SRC_DIR)/arena.h $(SRC_DIR)/jit.c $(SRC_DIR)/jit.h $(SRC_DIR)/opt.c $(SRC_DIR)/opt.h
	$(FLEX) -o $(SRC_DIR)/lex.yy.c $(SRC_DIR)/glowlang.l
	$(BISON) -d -o $(SRC_DIR)/glowlang.tab.c $(SRC_DIR)/glowlang.y
	$(CC) $(SRC_DIR)/lex.yy.c $(SRC_DIR)/glowlang.tab.c $(SRC_DIR)/lsp.c $(SRC_DIR)/arena.c $(SRC_DIR)/jit.c $(SRC_DIR)/opt.c -o $(GLOWLANG_BIN) $(CFLAGS) -lfl -lsqlite3
	rm -f $(SRC_DIR)/lex.yy.c $(SRC_DIR)/glowlang.tab.c $(SRC_DIR)/glowlang.tab.h

$(GDU_BIN): $(TOOLS_DIR)/gdu.c
	$(CC) $(TOOLS_DIR)/gdu.c -o $(GDU_BIN) $(CFLAGS) -lsqlite3

clean:
	rm -f $(GLOWLANG_BIN) $(GDU_BIN)
