#include "engine.h"
#include <iostream>
#include <string>

int main(int argc, char **argv) {
  if (argc != 3) {
    std::cerr << "Usage: ./hippodrome <path to map> <path to grammar>\n";
    std::exit(EXIT_FAILURE);
  }

  std::string map_path{argv[1]};
  std::string grammar_path{argv[2]};

  Engine{}.analyze(map_path, grammar_path);
}
