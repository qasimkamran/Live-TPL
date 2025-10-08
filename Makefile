# Compiler and flags
CC := gcc
CFLAGS := -Wall -Wextra -std=c11 -D_POSIX_C_SOURCE=200809L -g

# Directories
SRC_DIR := .
BIN_DIR := bin

# Static library settings
LIB_NAME := tagger
LIB := $(BIN_DIR)/lib$(LIB_NAME).a
LIB_SRCS := $(wildcard $(SRC_DIR)/*_lib.c)
LIB_OBJS := $(patsubst $(SRC_DIR)/%.c,$(BIN_DIR)/%.o,$(LIB_SRCS))

LDLIBS += -lm

# Script/executable sources: all .c files except *_lib.c
SCRIPT_SRCS := $(filter-out $(LIB_SRCS),$(wildcard $(SRC_DIR)/*.c))
SCRIPT_EXES := $(patsubst $(SRC_DIR)/%.c,$(BIN_DIR)/%,$(SCRIPT_SRCS))
SCRIPT_LINKS := $(patsubst $(BIN_DIR)/%,%,$(SCRIPT_EXES))

.PHONY: all clean
.SECONDARY:

# Default target: build library and all scripts
all: $(LIB) $(SCRIPT_EXES) $(SCRIPT_LINKS)

$(BIN_DIR):
	@mkdir -p $@

# Build static library
$(LIB): $(LIB_OBJS) | $(BIN_DIR)
	ar rcs $@ $^

# Build scripts/executables
$(BIN_DIR)/%: $(BIN_DIR)/%.o $(LIB) | $(BIN_DIR)
	$(CC) $< $(LIB) $(LDLIBS) -o $@

# Compile sources to object files
$(BIN_DIR)/%.o: $(SRC_DIR)/%.c | $(BIN_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(SCRIPT_LINKS): %: $(BIN_DIR)/%
	ln -sf $< $@

# Clean build artifacts
clean:
	rm -rf $(BIN_DIR)
	rm -f $(SCRIPT_LINKS)
