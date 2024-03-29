CXX := clang++-17
CXXFLAGS := -std=c++20 -Wpedantic -Wall -Wextra -Wconversion -Wshadow -Wunused-parameter

all:
	$(CXX) $(CXXFLAGS) -O3 hippodrome.cpp -o hippodrome

debug:
	$(CXX) $(CXXFLAGS) -Og -D DEBUG hippodrome.cpp -o hippodrome

perf:
	$(CXX) $(CXXFLAGS) -O3 -D PERF hippodrome.cpp -o hippodrome

prep:
	$(CXX) $(CXXFLAGS) -O3 preprocess.cpp -o preprocess
