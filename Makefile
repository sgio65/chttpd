CC      = cc
CFLAGS  = -ansi -Wall -Wextra -pedantic -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 -pthread
LDFLAGS =

SRC_DIR   = src
BUILD_DIR = build
BIN_DIR = bin
TARGET    = $(BIN_DIR)/chttpd

SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS))
DEPS = $(wildcard $(SRC_DIR)/*.h)

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c $(DEPS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -rf $(BUILD_DIR) $(TARGET)
