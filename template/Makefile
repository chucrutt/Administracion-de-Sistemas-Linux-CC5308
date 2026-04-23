CC = gcc
CFLAGS = -Wall -Wextra -std=c11
SRC = shell.c
OUT = shell

all: $(OUT)

$(OUT): $(SRC)
	$(CC) $(CFLAGS) -o $(OUT) $(SRC)

run: $(OUT)
	./$(OUT)

clean:
	rm -f $(OUT)

