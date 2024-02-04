CXX := g++-12
CXXFLAGS := -std=c++20 -Wpedantic -Wall -Wextra -Wconversion -Wshadow -Werror

all:
	$(CXX) $(CXXFLAGS) -O3 hippodrome.cpp -o hippodrome

debug:
	$(CXX) $(CXXFLAGS) -Og -D DEBUG hippodrome.cpp -o hippodrome
