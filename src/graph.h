#ifndef GRAPH_H
#define GRAPH_H

#include "event.h"
#include "grammar.h"
#include "transaction.h"
#include <unordered_map>
#include <unordered_set>
#include <utility>

struct Graph {
  std::unordered_map<Nonterminal, std::unordered_set<Transaction>> vertices;
  std::unordered_map<Nonterminal, std::unordered_set<Transaction>>
      reversed_vertices;
  std::unordered_map<
      Nonterminal, std::unordered_map<std::pair<Thread, Operand>, Transaction>>
      first_transaction;
  std::unordered_map<
      Nonterminal, std::unordered_map<std::pair<Thread, Operand>, Transaction>>
      last_transaction;
  std::unordered_set<std::pair<Transaction, Transaction>> edges;

  bool cyclic() {
    // TODO
    return false;
  }
};

#endif
