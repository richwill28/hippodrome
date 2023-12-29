#ifndef GRAPH_H
#define GRAPH_H

#include "event.h"
#include "grammar.h"
#include "transaction.h"
#include <stack>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct std::hash<std::pair<Thread, Operand>> {
  std::size_t operator()(const std::pair<Thread, Operand> &key) const {
    return std::hash<std::string>{}(key.first + " " + key.second);
  }
};

struct std::hash<std::pair<Transaction, Transaction>> {
  std::size_t operator()(const std::pair<Transaction, Transaction> &key) const {
    return std::hash<std::string>{}(std::to_string(key.first.idx) + " " +
                                    std::to_string(key.second.idx));
  }
};

struct Graph {
  std::unordered_set<Transaction> vertices;
  std::unordered_set<Transaction> reversed_vertices;
  std::unordered_map<std::pair<Thread, Operand>, Transaction> first_transaction;
  std::unordered_map<std::pair<Thread, Operand>, Transaction> last_transaction;
  std::unordered_set<std::pair<Transaction, Transaction>> edges;

  Graph()
      : vertices{}, reversed_vertices{}, first_transaction{},
        last_transaction{}, edges{} {}

  bool cyclic() {
    std::unordered_map<int, std::vector<int>> neighbors;
    std::unordered_map<int, int> in_degree;
    std::stack<int> bag;
    int visited = 0;

    for (const auto &edge : edges) {
      neighbors[edge.first.idx].push_back(edge.second.idx);
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
