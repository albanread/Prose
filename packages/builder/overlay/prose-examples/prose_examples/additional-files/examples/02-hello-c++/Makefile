# 02-hello-c++

NAME = hello
SRC  = hello.cpp

# make already has a CXX (g++), so this is set, not defaulted;
# "make CXX=..." on the command line still wins over it.
CXX = clang++
CXXFLAGS ?= -O2 -Wall -std=c++17

all: $(NAME)

$(NAME): $(SRC)
	$(CXX) $(CXXFLAGS) -o $@ $(SRC)

clean:
	rm -f $(NAME)

.PHONY: all clean
