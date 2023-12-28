#include "parser.h"
#include <string>

namespace engine {
void analyze(std::string map_path, std::string grammar_path) {
  Parser parser = Parser{};
  parser.parse(map_path, grammar_path);
}
} // namespace engine
