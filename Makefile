CC=gcc
CFLAGS=-Wall -Werror -Wextra -pedantic
CFLAGS+=-std=c99
CPPFLAGS=
LDFLAGS=
LDLIBS=

TARGET=ped
BUILD_DIR=./build
SRC=main.c
OBJ=$(SRC:%.c=$(BUILD_DIR)/%.o)
DEP=$(OBJ:%.o=%.d)

.PHONY: all clean

all: $(BUILD_DIR)/$(TARGET)
	ln -sf $(BUILD_DIR)/$(TARGET)

$(BUILD_DIR)/$(TARGET): $(OBJ)
	mkdir -p $(BUILD_DIR)
	$(CC) $(LDFLAGS) $^ -o $@ $(LDLIBS)

$(BUILD_DIR)/%.o: %.c
	mkdir -p $(BUILD_DIR)
	$(CC) -c -MMD $(CPPFLAGS) $(CFLAGS) $< -o $@

-include $(DEP)

clean:
	rm -rf $(BUILD_DIR) $(TARGET)
