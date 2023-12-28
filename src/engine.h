#ifndef ENGINE_H
#define ENGINE_H

#include "grammar.h"
#include "graph.h"
#include "parser.h"
#include <string>
#include <unordered_set>
#include <vector>

struct Engine {
  Grammar grammar;
  std::vector<Nonterminal> topological_ordering;
  std::unordered_map<Nonterminal, Graph> aux_graph;
  std::unordered_map<Nonterminal, Graph> cross_graph;
  std::unordered_map<Nonterminal, bool> csv;

  void compute_aux_graph(Nonterminal nonterminal) {
    if (aux_graph.contains(nonterminal)) {
      return;
    }

    // TODO
  }

  void compute_cross_graph(Nonterminal nonterminal) {
    if (cross_graph.contains(nonterminal)) {
      return;
    }

    // TODO
  }

  bool analyze_csv(Nonterminal nonterminal) {
    std::vector<Symbol> chunks = grammar.rules[nonterminal];

    if (chunks.size() == 1) {
      csv[nonterminal] = false;
      compute_aux_graph(nonterminal);
      return csv[nonterminal];

    } else if (chunks.size() == 2) {
      compute_cross_graph(nonterminal);
      csv[nonterminal] = cross_graph[nonterminal].cyclic();
      compute_aux_graph(nonterminal);
      return csv[nonterminal];
    }

    std::cerr << "Engine: Grammar is not in CNF\n";
    std::exit(EXIT_FAILURE);
  }

  void topological_sort_dfs(const Grammar &grammar, Nonterminal &vertex,
                            std::unordered_set<Nonterminal> &visited,
                            std::vector<Nonterminal> &result) {
    visited.insert(vertex);
    for (Symbol neighbor : grammar.rules.at(vertex)) {
      if (grammar.nonterminals.contains(neighbor)) {
        if (!visited.contains(neighbor)) {
          topological_sort_dfs(grammar, neighbor, visited, result);
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
    for (Nonterminal nonterminal : topological_ordering) {
      if (analyze_csv(nonterminal)) {
        std::cout << "Conflict serializability violation detected in " +
                         nonterminal + "\n";
        return;
      }
    }
    std::cout << "No conflict serializability violation detected\n";
  }
};

#endif
