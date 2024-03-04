CXX := clang++-17
CXXFLAGS := -std=c++20 -Wpedantic -Wall -Wextra -Wconversion -Wshadow -Wunused-parameter -Werror

all:
	$(CXX) $(CXXFLAGS) -O3 hippodrome.cpp -o hippodrome

debug:
	$(CXX) $(CXXFLAGS) -Og -D DEBUG hippodrome.cpp -o hippodrome
