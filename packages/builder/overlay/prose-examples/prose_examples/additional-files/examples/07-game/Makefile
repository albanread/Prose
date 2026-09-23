# 07-game

NAME = game
SRC  = game.cpp

# make already has a CXX (g++), so this is set, not defaulted;
# "make CXX=..." on the command line still wins over it.
CXX = clang++
CXXFLAGS ?= -O2 -Wall -std=c++17
LIBS = -lbe -lgame

all: $(NAME)

$(NAME): $(SRC)
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) $(LIBS)

clean:
	rm -f $(NAME)

.PHONY: all clean
