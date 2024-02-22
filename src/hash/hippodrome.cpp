#include "engine.h"
#include <chrono>
#include <iostream>
#include <string>
#include <sys/resource.h>

int main(int argc, char **argv) {
  if (argc != 3) {
    std::cerr << "Usage: ./hippodrome <path to map> <path to grammar>\n";
    std::exit(EXIT_FAILURE);
  }

  const rlim_t min_stack_size = 128 * 1024 * 1024; // 128 MB
  struct rlimit rlimits;
  if (getrlimit(RLIMIT_STACK, &rlimits) == 0) {
    if (rlimits.rlim_cur < min_stack_size) {
      rlimits.rlim_cur = min_stack_size;
      if (setrlimit(RLIMIT_STACK, &rlimits) != 0) {
        std::cerr << "Hippodrome: Failed setting stack size to "
                  << min_stack_size << "\n";
        std::exit(EXIT_FAILURE);
      }
    }
  }

  std::string map_path{argv[1]};
  std::string grammar_path{argv[2]};

  auto time_begin = std::chrono::steady_clock::now();

  Engine engine{};
  engine.analyze(map_path, grammar_path);

  auto time_end = std::chrono::steady_clock::now();

  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                      time_end - time_begin)
                      .count();

  std::cout << "Time elapsed = " << duration << " msec\n";
}
