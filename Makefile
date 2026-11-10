CFLAGS = -std=c11 -Wall -Wextra -pedantic -O2
TARGET = banker

all: $(TARGET)

$(TARGET): banker.c
	$(CC) $(CFLAGS) -o $(TARGET) banker.c

clean:
	rm -f $(TARGET) *.o

.PHONY: all clean