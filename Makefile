CC = gcc
CFLAGS = -Wall -Wextra -Wno-discarded-qualifiers -g -O2 -std=c99 -fPIC -fpie -D_GNU_SOURCE

SRC_DIR = src
OBJ_DIR = obj
TARGET = pcc

SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SRCS))

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $(TARGET)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR):
	mkdir $(OBJ_DIR)

clean:
	rm -rf $(OBJ_DIR) $(TARGET) *.asm *.o

.PHONY: all clean
