#ifndef GRAPH_H
#define GRAPH_H

#include "event.h"
#include "grammar.h"
#include "transaction.h"
#include <stack>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>

template <> struct std::hash<std::tuple<Thread, EventType, Operand>> {
  std::size_t
  operator()(const std::tuple<Thread, EventType, Operand> &key) const {
    return std::hash<std::string>{}(std::get<0>(key) + " " +
                                    event_type_to_string(std::get<1>(key)) +
                                    " " + std::get<2>(key));
  }
};

template <> struct std::hash<std::pair<Transaction, Transaction>> {
  std::size_t operator()(const std::pair<Transaction, Transaction> &key) const {
    return std::hash<std::string>{}(std::to_string(key.first.idx) + " " +
                                    std::to_string(key.second.idx));
  }
};

struct Graph {
  std::unordered_set<Transaction> vertices;
  std::unordered_set<Transaction> reversed_vertices;
  std::unordered_map<std::tuple<Thread, EventType, Operand>, Transaction>
      first_transaction;
  std::unordered_map<std::tuple<Thread, EventType, Operand>, Transaction>
      last_transaction;
  std::unordered_set<std::pair<Transaction, Transaction>> edges;
  std::unordered_map<Transaction, std::unordered_set<Event>> content;
  std::unordered_map<Transaction, std::unordered_set<Event>> summary;
  std::unordered_map<Transaction, std::unordered_set<Event>> reversed_summary;

  Graph()
      : vertices{}, reversed_vertices{}, first_transaction{},
        last_transaction{}, edges{}, content{}, summary{}, reversed_summary{} {}

  bool cyclic() {
    std::unordered_map<int, std::unordered_set<int>> neighbors;
    std::unordered_map<int, size_t> in_degree;
    std::stack<int> bag;
    size_t visited = 0;

    for (const auto &edge : edges) {
      neighbors[edge.first.idx].insert(edge.second.idx);
      in_degree[edge.second.idx]++;
    }

    for (const auto &vertex : vertices) {
      if (in_degree[vertex.idx] == 0) {
        bag.push(vertex.idx);
      }
    }

    while (!bag.empty()) {
      int v = bag.top();
      bag.pop();
      visited++;
      for (int w : neighbors[v]) {
        in_degree[w]--;
        if (in_degree[w] == 0) {
          bag.push(w);
        }
      }
    }

    return visited < vertices.size();
  }
};

#endif
