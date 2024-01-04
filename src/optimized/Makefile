CXX := clang++
CXXFLAGS := -std=c++2a -O3 -Wall -Wextra -Wpedantic

all:
	$(CXX) $(CXXFLAGS) hippodrome.cpp -o hippodrome

debug:
	$(CXX) $(CXXFLAGS) -g -D DEBUG hippodrome.cpp -o hippodrome
