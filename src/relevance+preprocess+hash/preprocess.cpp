#include "grammar.h"
#include "parser.h"
#include <ankerl/unordered_dense.h>
#include <chrono>
#include <iostream>
#include <memory>
#include <string>

struct Preprocess {
  Grammar grammar;
  std::deque<Nonterminal> topological_ordering;
  std::vector<Nonterminal> reverse_topological_ordering;

  ankerl::unordered_dense::set<Thread> threads;
  ankerl::unordered_dense::set<Operand> variables;
  ankerl::unordered_dense::set<Operand> locks;

  ankerl::unordered_dense::map<Nonterminal,
                               ankerl::unordered_dense::set<Operand>>
      operands;
  ankerl::unordered_dense::map<Operand,
                               ankerl::unordered_dense::set<Nonterminal>>
      lowest_occurences;
  ankerl::unordered_dense::map<Nonterminal,
                               ankerl::unordered_dense::set<Nonterminal>>
      parents;
  ankerl::unordered_dense::map<Nonterminal,
                               ankerl::unordered_dense::set<Nonterminal>>
      ancestors;

  ankerl::unordered_dense::map<Operand, Nonterminal> lowest_common_ancestor;
  ankerl::unordered_dense::map<Nonterminal,
                               ankerl::unordered_dense::set<Operand>>
      maintain;

  Preprocess()
      : grammar{}, reverse_topological_ordering{}, threads{}, variables{},
        locks{}, operands{}, lowest_occurences{}, parents{}, ancestors{},
        lowest_common_ancestor{}, maintain{} {}

  void compute_lowest_occurences(Nonterminal nonterminal) {
    for (const Terminal &term : grammar.rules[nonterminal]) {
      Event event = grammar.content[term];
      // For now we keep track of variables and locks.
      // TODO: Maybe threads as well?
      if (event.type == EventType::read || event.type == EventType::write ||
          event.type == EventType::lock || event.type == EventType::unlock) {
        operands[nonterminal].insert(event.operand);
        lowest_occurences[event.operand].insert(nonterminal);
      }
    }
  }

  void compute_parents_dfs(Nonterminal vertex,
                           ankerl::unordered_dense::set<Nonterminal> &visited) {
    visited.insert(vertex);
    for (Symbol neighbor : grammar.rules.at(vertex)) {
      if (grammar.nonterminals.contains(neighbor)) {
        parents[neighbor].insert(vertex);
        if (!visited.contains(neighbor)) {
          compute_parents_dfs(neighbor, visited);
        }
      }
    }
  }

  void compute_parents() {
    Nonterminal start{"0"};
    ankerl::unordered_dense::set<Nonterminal> visited;
    compute_parents_dfs(start, visited);
  }

  void compute_ancestors() {
    size_t th = 1;
    ancestors["0"] = ankerl::unordered_dense::set<Operand>();
    for (const auto &vertex : topological_ordering) {
      std::cout << "Stage 2b: " << th++ << "/" << topological_ordering.size()
                << "\n";
      for (const auto &parent : parents[vertex]) {
        ancestors[vertex].insert(parent);
        ancestors[vertex].insert(ancestors[parent].begin(),
                                 ancestors[parent].end());
      }
    }
  }

  void
  compute_ancestor_dfs(Nonterminal start, Nonterminal vertex,
                       ankerl::unordered_dense::set<Nonterminal> &visited) {

    visited.insert(vertex);

    if (vertex == "0") {
      return;
    }

    for (const auto &p : parents.at(vertex)) {
      ancestors[start].insert(p);
      if (!visited.contains(p)) {
        compute_ancestor_dfs(start, p, visited);
      }
    }
  }

  void compute_ancestor(Nonterminal start) {

    ankerl::unordered_dense::set<Nonterminal> visited;
    compute_ancestor_dfs(start, start, visited);
  }

  void compute_lowest_common_ancestor_dfs(
      Nonterminal vertex, Nonterminal &lca,
      const ankerl::unordered_dense::set<Nonterminal> &common_ancestors) {

    Nonterminal top_child = grammar.rules[vertex][0];
    Nonterminal bottom_child = grammar.rules[vertex][1];

    bool has_top_child = common_ancestors.contains(top_child);
    bool has_bottom_child = common_ancestors.contains(bottom_child);

    if (has_top_child && has_bottom_child) {
      lca = vertex;
    } else if (has_top_child) {
      compute_lowest_common_ancestor_dfs(top_child, lca, common_ancestors);
    } else if (has_bottom_child) {
      compute_lowest_common_ancestor_dfs(bottom_child, lca, common_ancestors);
    } else {
      lca = vertex;
    }
  }

  void compute_lowest_common_ancestor(Operand operand) {

    if (lowest_common_ancestor.contains(operand)) {
      return;
    }

    ankerl::unordered_dense::set<Nonterminal> common_ancestors;

    bool first = true;

    for (const auto &nont : lowest_occurences[operand]) {
      if (first) {
        common_ancestors.insert(ancestors.at(nont).begin(),
                                ancestors.at(nont).end());
        first = false;
      } else {
        ankerl::unordered_dense::set<Nonterminal> intersection;
        for (const auto &anct : ancestors.at(nont)) {
          if (common_ancestors.contains(anct)) {
            intersection.insert(anct);
          }
        }
        common_ancestors = intersection;
      }
    }

#ifdef DEBUG
    std::cout << "Lowest occurences of " << operand << "\n";
    for (const auto &nont : lowest_occurences[operand]) {
      std::cout << nont << " ";
    }
    std::cout << "\n";

    std::cout << "Common ancestors of " << operand << "\n";
    for (const auto &anct : common_ancestors) {
      std::cout << anct << " ";
    }
    std::cout << "\n";
#endif

    Nonterminal start{"0"};
    compute_lowest_common_ancestor_dfs(start, lowest_common_ancestor[operand],
                                       common_ancestors);

#ifdef DEBUG
    std::cout << "Lowest common ancestor of " << operand << " = "
              << lowest_common_ancestor[operand] << "\n\n";
#endif
  }

  void compute_relevance_base(Nonterminal nonterminal) {
    for (const Operand &op : operands[nonterminal]) {
      if (lowest_common_ancestor[op] != nonterminal) {
        maintain[nonterminal].insert(op);
      }
    }

#ifdef DEBUG
    std::cout << "Relevant operands to maintain from " << nonterminal << "\n";
    for (const auto &op : maintain[nonterminal]) {
      std::cout << op << " ";
    }
    std::cout << "\n\n";
#endif
  }

  void compute_relevance_inductive(Nonterminal nonterminal) {
    Nonterminal top_child = grammar.rules[nonterminal][0];
    Nonterminal bottom_child = grammar.rules[nonterminal][1];

    for (const Operand &op : maintain[top_child]) {
      if (lowest_common_ancestor[op] != nonterminal) {
        maintain[nonterminal].insert(op);
      }
    }

    for (const Operand &op : maintain[bottom_child]) {
      if (lowest_common_ancestor[op] != nonterminal) {
        maintain[nonterminal].insert(op);
      }
    }

#ifdef DEBUG
    std::cout << "Relevant operands to maintain from " << nonterminal << "\n";
    for (const auto &op : maintain[nonterminal]) {
      std::cout << op << " ";
    }
    std::cout << "\n\n";
#endif
  }

  void preprocess() {
    ankerl::unordered_dense::set<Nonterminal> lowest_nonterminals;
    for (const Nonterminal &nonterminal : reverse_topological_ordering) {
      if (grammar.terminals.contains(grammar.rules[nonterminal].front())) {
        lowest_nonterminals.insert(nonterminal);
      }
    }

    auto time_begin = std::chrono::high_resolution_clock::now();

    std::cout << "Stage 1: Compute lowest occurences of operands\n";

    // 1. Compute lowest occurences of operands
    for (const Nonterminal &nonterminal : lowest_nonterminals) {
      compute_lowest_occurences(nonterminal);
    }

    auto time_end = std::chrono::high_resolution_clock::now();

    auto duration =
        std::chrono::duration_cast<std::chrono::seconds>(time_end - time_begin)
            .count();

    std::cout << "Time elapsed = " << duration << " sec\n\n";

    std::cout << "Stage 2: Compute lowest common ancestor\n";

    // 2. Compute lowest common ancestor
    std::cout << "Stage 2a: Compute parents\n";

    time_begin = std::chrono::high_resolution_clock::now();

    compute_parents();

    time_end = std::chrono::high_resolution_clock::now();

    duration =
        std::chrono::duration_cast<std::chrono::seconds>(time_end - time_begin)
            .count();

    std::cout << "Time elapsed = " << duration << " sec\n\n";

    std::cout << "Stage 2b: Compute ancestors\n";

    time_begin = std::chrono::high_resolution_clock::now();

    compute_ancestors();

    time_end = std::chrono::high_resolution_clock::now();

    duration =
        std::chrono::duration_cast<std::chrono::seconds>(time_end - time_begin)
            .count();

    std::cout << "Time elapsed = " << duration << " sec\n\n";

    ankerl::unordered_dense::set<Operand> ops;
    ops.insert(variables.begin(), variables.end());
    ops.insert(locks.begin(), locks.end());

    for (const Operand &op : ops) {
      if (lowest_occurences[op].size() == 1) {
        lowest_common_ancestor[op] = *(lowest_occurences[op].begin());

#ifdef DEBUG
        std::cout << "Lowest common ancestor of " << op << " = "
                  << lowest_common_ancestor[op] << "\n\n";
#endif
      }
    }

    std::cout << "Stage 2c: Compute lowest common ancestors\n";

    time_begin = std::chrono::high_resolution_clock::now();

    size_t th = 1;
    for (const auto &op : ops) {
      std::cout << "Stage 2c: " << th++ << "/" << ops.size() << "\n";
      compute_lowest_common_ancestor(op);
    }

    time_end = std::chrono::high_resolution_clock::now();

    duration =
        std::chrono::duration_cast<std::chrono::seconds>(time_end - time_begin)
            .count();

    std::cout << "Time elapsed = " << duration << " sec\n\n";

    // 3. Compute relevance
    std::cout << "Stage 3: Compute relevance\n";

    time_begin = std::chrono::high_resolution_clock::now();

    for (const Nonterminal &nonterminal : reverse_topological_ordering) {
      if (lowest_nonterminals.contains(nonterminal)) {
        compute_relevance_base(nonterminal);
      } else {
        compute_relevance_inductive(nonterminal);
      }
    }

    time_end = std::chrono::high_resolution_clock::now();

    duration =
        std::chrono::duration_cast<std::chrono::seconds>(time_end - time_begin)
            .count();

    std::cout << "Time elapsed = " << duration << " sec\n\n";
  }

  void
  topological_sort_dfs(Nonterminal vertex,
                       ankerl::unordered_dense::set<Nonterminal> &visited) {
    visited.insert(vertex);
    for (Symbol neighbor : grammar.rules.at(vertex)) {
      if (grammar.nonterminals.contains(neighbor)) {
        if (!visited.contains(neighbor)) {
          topological_sort_dfs(neighbor, visited);
        }
      }
    }
    topological_ordering.push_front(vertex);
    reverse_topological_ordering.push_back(vertex);
  }

  void topological_sort() {
    Nonterminal start{"0"};
    ankerl::unordered_dense::set<Nonterminal> visited;
    topological_sort_dfs(start, visited);
  }

  void write(std::string output_path) {
    // 4. Write to file
    std::cout << "Stage 4: Write to file\n";

    std::ofstream file{output_path};
    for (const auto &[nont, ops] : maintain) {
      file << nont << " -> ";
      for (const auto &op : ops) {
        file << op << " ";
      }
      file << "\n";
    }
  }

  void run(std::string map_path, std::string grammar_path,
           std::string output_path) {
    std::unique_ptr<Parser> parser{std::make_unique<Parser>()};
    parser->parse(map_path, grammar_path);

    grammar = parser->grammar;
    threads = parser->threads;
    variables = parser->variables;
    locks = parser->locks;

    topological_sort();
    preprocess();
    write(output_path);
  }
};

int main(int argc, char **argv) {
  if (argc != 4) {
    std::cerr << "Usage: ./preprocess <path to map> <path to grammar> <path to "
                 "output>\n";
    std::exit(EXIT_FAILURE);
  }

  std::string map_path{argv[1]};
  std::string grammar_path{argv[2]};
  std::string output_path{argv[3]};

  std::unique_ptr<Preprocess> prep{std::make_unique<Preprocess>()};
  prep->run(map_path, grammar_path, output_path);
}
