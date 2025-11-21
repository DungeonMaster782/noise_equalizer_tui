CC = gcc
CFLAGS = -std=c11 -Wall -O2 -pthread
LDFLAGS = -lncurses -lasound -lm

TARGET = white_noise
SRC = main.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS)

clean:
	rm -f $(TARGET)
