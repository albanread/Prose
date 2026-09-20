# 01-hello

NAME = hello
SRC  = hello.c

# make already has a CC and a CXX (cc and g++), so these are set, not
# defaulted; "make CXX=..." on the command line still wins over both.
CC = clang
CFLAGS ?= -O2 -Wall

all: $(NAME)

$(NAME): $(SRC)
	$(CC) $(CFLAGS) -o $@ $(SRC)

clean:
	rm -f $(NAME)

.PHONY: all clean
