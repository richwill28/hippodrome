#ifndef ENGINE_H
#define ENGINE_H

#include "event.h"
#include "grammar.h"
#include "graph.h"
#include "parser.h"
#include <string>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <vector>

template <> struct std::hash<std::pair<Nonterminal, Transaction>> {
  std::size_t operator()(const std::pair<Nonterminal, Transaction> &key) const {
    return std::hash<std::string>{}(key.first + " " +
                                    std::to_string(key.second.idx));
  }
};

struct Engine {
  Grammar grammar;
  std::vector<Nonterminal> topological_ordering;

  std::unordered_map<Nonterminal, Graph> aux_graph;
  std::unordered_map<Nonterminal, Graph> cross_graph;
  std::unordered_map<Nonterminal, bool> csv;

  std::unordered_set<Thread> threads;
  std::unordered_set<Operand> variables;
  std::unordered_set<Operand> locks;

  int fresh_transaction_idx = 0;

  Transaction make_transaction() {
    return Transaction{fresh_transaction_idx++};
  }

  void compute_aux_graph_one(Nonterminal A, Terminal a) {
    Event event = grammar.content[a];

    if (event.type == EventType::begin) {
      event.annotation = Annotation::down;
    } else if (event.type == EventType::end) {
      event.annotation = Annotation::up;
    } else {
      event.annotation = Annotation::open;
    }

    Transaction transaction = make_transaction();
    transaction.thread = event.thread;

    if (event.type != EventType::end) {
      aux_graph[A].vertices.insert(transaction);
    }

    if (event.type != EventType::begin) {
      aux_graph[A].reversed_vertices.insert(transaction);
    }

    aux_graph[A].first_transaction[std::make_tuple(
        event.thread, EventType::undefined, "")] = transaction;
    aux_graph[A].first_transaction[std::make_tuple(
        event.thread, event.type, event.operand)] = transaction;

    aux_graph[A].last_transaction[std::make_tuple(
        event.thread, EventType::undefined, "")] = transaction;
    aux_graph[A].last_transaction[std::make_tuple(event.thread, event.type,
                                                  event.operand)] = transaction;

    aux_graph[A].content[transaction].insert(event);
  }

  void compute_aux_graph_two(Nonterminal A, Nonterminal B, Nonterminal C) {
    std::unordered_map<Thread, Transaction> cross_transaction;
    std::unordered_map<Transaction, Transaction> parent;
    // std::unordered_map<Transaction, Transaction> sibling;

    for (const Transaction &vertex : aux_graph[B].vertices) {
      Transaction txn = make_transaction();
      txn.thread = vertex.thread;
      aux_graph[B].content[txn] = aux_graph[B].content[vertex];
      aux_graph[A].content[txn] = aux_graph[B].content[vertex];
      cross_transaction[txn.thread] = txn;
      parent[vertex] = txn;
    }

    for (const Transaction &reversed_vertex : aux_graph[C].reversed_vertices) {
      if (cross_transaction.contains(reversed_vertex.thread)) {

        if (cross_transaction[reversed_vertex.thread].thread !=
            reversed_vertex.thread) {
          std::cerr << "Engine: Something went wrong\n";
          std::exit(EXIT_FAILURE);
        }

        Transaction txn = cross_transaction[reversed_vertex.thread];
        aux_graph[C].content[txn] = aux_graph[C].content[reversed_vertex];
        aux_graph[A].content[txn].insert(
            aux_graph[C].content[reversed_vertex].begin(),
            aux_graph[C].content[reversed_vertex].end());
        parent[reversed_vertex] = txn;

      } else {
        Transaction txn = make_transaction();
        txn.thread = reversed_vertex.thread;
        aux_graph[C].content[txn] = aux_graph[C].content[reversed_vertex];
        aux_graph[A].content[txn] = aux_graph[C].content[reversed_vertex];
        cross_transaction[txn.thread] = txn;
        parent[reversed_vertex] = txn;
      }
    }

    for (const Transaction &vertex : aux_graph[C].vertices) {
      if (parent.contains(vertex)) {
        aux_graph[A].vertices.insert(parent[vertex]);
      } else {
        parent[vertex] = vertex;
        aux_graph[A].vertices.insert(parent[vertex]);
        aux_graph[A].content[parent[vertex]] =
            aux_graph[C].content[parent[vertex]];
      }
    }

    for (const Transaction &vertex : aux_graph[B].vertices) {
      for (const Event &event : aux_graph[A].content[parent[vertex]]) {
        if (event.type == EventType::end) {
          continue;
        }
      }
      aux_graph[A].vertices.insert(parent[vertex]);
    }

    for (const Transaction &reversed_vertex : aux_graph[B].reversed_vertices) {
      if (parent.contains(reversed_vertex)) {
        aux_graph[A].reversed_vertices.insert(parent[reversed_vertex]);
      } else {
        parent[reversed_vertex] = reversed_vertex;
        aux_graph[A].reversed_vertices.insert(parent[reversed_vertex]);
        aux_graph[A].content[parent[reversed_vertex]] =
            aux_graph[C].content[parent[reversed_vertex]];
      }
    }

    for (const Transaction &reversed_vertex : aux_graph[C].reversed_vertices) {
      for (const Event &event : aux_graph[A].content[parent[reversed_vertex]]) {
        if (event.type == EventType::begin) {
          continue;
        }
      }
      aux_graph[A].reversed_vertices.insert(parent[reversed_vertex]);
    }

    std::vector<std::tuple<Thread, EventType, Operand>> first_args;
    for (const Thread &thread : threads) {
      first_args.push_back(std::make_tuple(thread, EventType::undefined, ""));
      for (const Operand &variable : variables) {
        first_args.push_back(
            std::make_tuple(thread, EventType::read, variable));
        first_args.push_back(
            std::make_tuple(thread, EventType::write, variable));
      }
      for (const Operand &lock : locks) {
        first_args.push_back(std::make_tuple(thread, EventType::lock, lock));
      }
      for (const Thread &thread : threads) {
        first_args.push_back(std::make_tuple(thread, EventType::join, thread));
      }
    }

    for (const auto &arg : first_args) {
      if (aux_graph[B].first_transaction.contains(arg)) {
        Transaction txn = aux_graph[B].first_transaction[arg];
        if (parent.contains(txn)) {
          aux_graph[A].first_transaction[arg] = parent[txn];
        } else {
          parent[txn] = txn;
          aux_graph[A].first_transaction[arg] = parent[txn];
          aux_graph[A].content[parent[txn]] = aux_graph[B].content[parent[txn]];
        }

      } else if (aux_graph[C].first_transaction.contains(arg)) {
        Transaction txn = aux_graph[C].first_transaction[arg];
        if (parent.contains(txn)) {
          aux_graph[A].first_transaction[arg] = parent[txn];
        } else {
          parent[txn] = txn;
          aux_graph[A].first_transaction[arg] = parent[txn];
          aux_graph[A].content[parent[txn]] = aux_graph[C].content[parent[txn]];
        }
      }
    }

    std::vector<std::tuple<Thread, EventType, Operand>> last_args;
    for (const Thread &thread : threads) {
      last_args.push_back(std::make_tuple(thread, EventType::undefined, ""));
      for (const Operand &variable : variables) {
        last_args.push_back(std::make_tuple(thread, EventType::read, variable));
        last_args.push_back(
            std::make_tuple(thread, EventType::write, variable));
      }
      for (const Operand &lock : locks) {
        last_args.push_back(std::make_tuple(thread, EventType::unlock, lock));
      }
      for (const Thread &thread : threads) {
        last_args.push_back(std::make_tuple(thread, EventType::fork, thread));
      }
    }

    for (const auto &arg : last_args) {
      if (aux_graph[C].last_transaction.contains(arg)) {
        Transaction txn = aux_graph[C].last_transaction[arg];
        if (parent.contains(txn)) {
          aux_graph[A].last_transaction[arg] = parent[txn];
        } else {
          parent[txn] = txn;
          aux_graph[A].last_transaction[arg] = parent[txn];
          aux_graph[A].content[parent[txn]] = aux_graph[C].content[parent[txn]];
        }

      } else if (aux_graph[B].last_transaction.contains(arg)) {
        Transaction txn = aux_graph[B].last_transaction[arg];
        if (parent.contains(txn)) {
          aux_graph[A].last_transaction[arg] = parent[txn];
        } else {
          parent[txn] = txn;
          aux_graph[A].last_transaction[arg] = parent[txn];
          aux_graph[A].content[parent[txn]] = aux_graph[B].content[parent[txn]];
        }
      }
    }
  }

  void compute_aux_graph(Nonterminal nonterminal) {
    if (aux_graph.contains(nonterminal)) {
      return;
    }

    std::vector<Nonterminal> chunks = grammar.rules[nonterminal];

    if (chunks.size() == 1) {
      compute_aux_graph_one(nonterminal, chunks[0]);
    } else if (chunks.size() == 2) {
      compute_aux_graph_two(nonterminal, chunks[0], chunks[1]);
    } else {
      std::cerr << "Engine: Something went wrong\n";
      std::exit(EXIT_FAILURE);
    }
  }

  void compute_cross_graph(Nonterminal A) {
    if (cross_graph.contains(A)) {
      return;
    }

    std::vector<Nonterminal> chunks = grammar.rules[A];
    if (chunks.size() != 2) {
      std::cerr << "Engine: Something went wrong\n";
      std::exit(EXIT_FAILURE);
    }

    Nonterminal B{chunks[0]};
    Nonterminal C{chunks[1]};

    std::unordered_map<Thread, Transaction> cross_transaction;
    std::unordered_map<std::pair<Nonterminal, Transaction>,
                       std::unordered_set<Event>>
        content;
    std::unordered_map<std::pair<Nonterminal, Transaction>,
                       std::unordered_set<Event>>
        summary;
    std::unordered_map<std::pair<Nonterminal, Transaction>,
                       std::unordered_set<Event>>
        reversed_summary;

    for (const Transaction &vertex : aux_graph[B].vertices) {
      cross_transaction[vertex.thread] = make_transaction();
      cross_transaction[vertex.thread].thread = vertex.thread;
      content[std::make_pair(B, cross_transaction[vertex.thread])] =
          aux_graph[B].content[vertex];
      summary[std::make_pair(B, cross_transaction[vertex.thread])] =
          aux_graph[B].summary[vertex];
    }

    for (const Transaction &reversed_vertex : aux_graph[C].reversed_vertices) {
      if (cross_transaction.contains(reversed_vertex.thread)) {

        if (cross_transaction[reversed_vertex.thread].thread !=
            reversed_vertex.thread) {
          std::cerr << "Engine: Something went wrong\n";
          std::exit(EXIT_FAILURE);
        }

        content[std::make_pair(C, cross_transaction[reversed_vertex.thread])] =
            aux_graph[C].content[reversed_vertex];
        reversed_summary[std::make_pair(
            C, cross_transaction[reversed_vertex.thread])] =
            aux_graph[C].reversed_summary[reversed_vertex];

      } else {
        cross_transaction[reversed_vertex.thread] = make_transaction();
        cross_transaction[reversed_vertex.thread].thread =
            reversed_vertex.thread;
        content[std::make_pair(C, cross_transaction[reversed_vertex.thread])] =
            aux_graph[C].content[reversed_vertex];
        reversed_summary[std::make_pair(
            C, cross_transaction[reversed_vertex.thread])] =
            aux_graph[C].reversed_summary[reversed_vertex];
      }
    }

    for (const std::pair<const Thread, Transaction> &it : cross_transaction) {
      cross_graph[A].vertices.insert(it.second);
    }

    for (const std::pair<Transaction, Transaction> &edge : aux_graph[B].edges) {
      if (aux_graph[B].vertices.contains(edge.first) &&
          aux_graph[B].vertices.contains(edge.second)) {
        cross_graph[A].edges.insert(
            std::make_pair(cross_transaction[edge.first.thread],
                           cross_transaction[edge.second.thread]));
      }
    }

    for (const std::pair<Transaction, Transaction> &edge : aux_graph[C].edges) {
      if (aux_graph[C].reversed_vertices.contains(edge.first) &&
          aux_graph[C].reversed_vertices.contains(edge.second)) {
        cross_graph[A].edges.insert(
            std::make_pair(cross_transaction[edge.first.thread],
                           cross_transaction[edge.second.thread]));
      }
    }

    for (const Transaction &v : cross_graph[A].vertices) {
      for (const Transaction &w : cross_graph[A].vertices) {
        if (v != w) {
          for (const Event &e : content[std::make_pair(B, v)]) {
            for (const Event &f : content[std::make_pair(C, w)]) {
              if (e.conflict(f)) {
                cross_graph[A].edges.insert(std::make_pair(v, w));
              }
            }
          }
        }

        for (const Event &e : content[std::make_pair(B, v)]) {
          for (const Event &f : reversed_summary[std::make_pair(C, w)]) {
            if (e.conflict(f)) {
              cross_graph[A].edges.insert(std::make_pair(v, w));
            }
          }
        }

        for (const Event &e : summary[std::make_pair(B, v)]) {
          for (const Event &f : content[std::make_pair(C, w)]) {
            if (e.conflict(f)) {
              cross_graph[A].edges.insert(std::make_pair(v, w));
            }
          }
        }

        for (const Event &e : summary[std::make_pair(B, v)]) {
          for (const Event &f : reversed_summary[std::make_pair(C, w)]) {
            if (e.conflict(f)) {
              cross_graph[A].edges.insert(std::make_pair(v, w));
            }
          }
        }
      }
    }
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
    std::tie(grammar, threads, variables, locks) =
        Parser{}.parse(map_path, grammar_path);
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
