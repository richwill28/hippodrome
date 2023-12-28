#ifndef ENGINE_H
#define ENGINE_H

#include "grammar.h"
#include "parser.h"
#include <string>
#include <unordered_set>
#include <vector>

struct Engine {
  Grammar grammar;
  std::vector<Nonterminal> topological_ordering;

  void topological_sort_dfs(const Grammar &grammar, Nonterminal &vertex,
                            std::unordered_set<Nonterminal> &visited,
                            std::vector<Nonterminal> &result) {
    visited.insert(vertex);
    for (Symbol symbol : grammar.rules.at(vertex)) {
      if (grammar.nonterminals.contains(symbol)) {
        if (!visited.contains(symbol)) {
          topological_sort_dfs(grammar, vertex, visited, result);
        }
      }
    }
    result.push_back(vertex);
  }

  std::vector<Nonterminal> topological_sort(const Grammar &grammar) {
    Nonterminal start{"0"};
    std::unordered_set<Nonterminal> visited;
    std::vector<Nonterminal> result;
    topological_sort_dfs(grammar, start, visited, result);
    return result;
  }

  void analyze(std::string map_path, std::string grammar_path) {
    grammar = Parser{}.parse(map_path, grammar_path);
    topological_ordering = topological_sort(grammar);
  }
};

#endif
