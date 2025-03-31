CC = gcc
CFLAGS = -Wall -Wextra -pedantic -std=c11
LDFLAGS = -pthread
INCLUDES = -I./src

# Debug build flags
ifeq ($(DEBUG),1)
	CFLAGS += -g -O0 -fsanitize=address -DDEBUG
else
	CFLAGS += -O3
endif

# Source files
SRC = src/main.c \
      src/proxy.c \
      src/instance_mgr.c \
      src/hash.c \
      src/protocol.c \
      src/connection.c \
      src/config.c

OBJ = $(SRC:.c=.o)

# Test source files
TEST_SRC = tests/test_main.c \
           tests/test_hash.c \
           tests/test_protocol.c \
           src/hash.c \
           src/protocol.c

TEST_OBJ = $(TEST_SRC:.c=.o)

# Output binary
BIN = threadis
TEST_BIN = test_main

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

test: $(TEST_BIN)
	./$(TEST_BIN)

$(TEST_BIN): $(TEST_OBJ)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(OBJ) $(TEST_OBJ) $(BIN) $(TEST_BIN)

# For debugging - print all variables
print-%:
	@echo $* = $($*)

.PHONY: all test clean
