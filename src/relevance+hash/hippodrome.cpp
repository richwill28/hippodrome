#include "engine.h"
#include <chrono>
#include <iostream>
#include <memory>
#include <string>

int main(int argc, char **argv) {
  if (argc != 3) {
    std::cerr << "Usage: ./hippodrome <path to map> <path to grammar>\n";
    std::exit(EXIT_FAILURE);
  }

  std::string map_path{argv[1]};
  std::string grammar_path{argv[2]};

  auto time_begin = std::chrono::high_resolution_clock::now();

  std::unique_ptr<Engine> engine{std::make_unique<Engine>()};
  engine->analyze(map_path, grammar_path);

  auto time_end = std::chrono::high_resolution_clock::now();

  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                      time_end - time_begin)
                      .count();

  std::cout << "Time elapsed = " << duration << " msec\n";
}
