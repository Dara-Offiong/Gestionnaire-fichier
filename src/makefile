CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -g
LDFLAGS = 
TARGET = myfilesystem
SOURCES = gestionnaire.c main.c
HEADERS = gestionnaire.h
OBJS = $(SOURCES:.c=.o)

.PHONY: all clean run test

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJS)

run: $(TARGET)
	./$(TARGET)

test: run
