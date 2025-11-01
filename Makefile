CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -O3 -march=native -flto -DNDEBUG
TARGET = mexplorer
SOURCES = main.c mexplorer.c

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) -o $(TARGET) $(SOURCES)

clean:
	rm -f $(TARGET)

.PHONY: clean
