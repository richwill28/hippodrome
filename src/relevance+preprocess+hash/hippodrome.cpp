#include "engine.h"
#include <chrono>
#include <iostream>
#include <memory>
#include <string>

int main(int argc, char **argv) {
  if (argc != 4) {
    std::cerr << "Usage: ./hippodrome <path to map> <path to grammar> <path to "
                 "preprocess>\n";
    std::exit(EXIT_FAILURE);
  }

  std::string map_path{argv[1]};
  std::string grammar_path{argv[2]};
  std::string prep_path{argv[3]};

  std::unique_ptr<Engine> engine{std::make_unique<Engine>()};
  engine->parse(map_path, grammar_path, prep_path);

  auto time_begin = std::chrono::high_resolution_clock::now();
  engine->analyze();
  auto time_end = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                      time_end - time_begin)
                      .count();

  std::cout << "Time elapsed = " << duration << " msec\n";
}
