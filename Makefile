CC = gcc
CFLAGS = -Wall -O2
LDFLAGS = -lasound -lncurses -lm -pthread
TARGET = white_noise
SRC = main.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LDFLAGS)

clean:
	rm -f $(TARGET)
