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

template <> struct std::hash<std::pair<Nonterminal, TransactionIdx>> {
  std::size_t
  operator()(const std::pair<Nonterminal, TransactionIdx> &key) const {
    return std::hash<std::string>{}(key.first + " " +
                                    std::to_string(key.second));
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

  int fresh_transaction_idx = 0;

  Transaction make_transaction() {
    return Transaction{fresh_transaction_idx++};
  }

  void compute_aux_graph(Nonterminal nonterminal) {
    if (aux_graph.contains(nonterminal)) {
      return;
    }

    std::vector<Nonterminal> chunks = grammar.rules[nonterminal];

    if (chunks.size() == 1) {
      Terminal chunk = chunks[0];
      Event event = grammar.content[chunk];

      if (event.type == EventType::begin) {
        event.annotation = Annotation::down;
      } else if (event.type == EventType::end) {
        event.annotation = Annotation::up;
      } else {
        event.annotation = Annotation::open;
      }

      Transaction transaction = make_transaction();
      transaction.thread = event.thread;
      transaction.content.insert(event);

      if (event.type != EventType::end) {
        aux_graph[nonterminal].vertices.insert(transaction);
      }

      if (event.type != EventType::begin) {
        aux_graph[nonterminal].reversed_vertices.insert(transaction);
      }

      aux_graph[nonterminal].first_transaction[std::make_tuple(
          event.thread, EventType::undefined, "")] = transaction;
      aux_graph[nonterminal].first_transaction[std::make_tuple(
          event.thread, event.type, event.operand)] = transaction;

      aux_graph[nonterminal].last_transaction[std::make_tuple(
          event.thread, EventType::undefined, "")] = transaction;
      aux_graph[nonterminal].last_transaction[std::make_tuple(
          event.thread, event.type, event.operand)] = transaction;

    } else if (chunks.size() == 2) {
      Nonterminal top_chunk = chunks[0];
      Nonterminal bottom_chunk = chunks[1];

    } else {
      std::cerr << "Engine: Grammar is not in CNF\n";
      std::exit(EXIT_FAILURE);
    }
  }

  void compute_cross_graph(Nonterminal nonterminal) {
    if (cross_graph.contains(nonterminal)) {
      return;
    }

    std::vector<Nonterminal> chunks = grammar.rules[nonterminal];
    if (chunks.size() != 2) {
      std::cerr << "Engine: Something went wrong\n";
      std::exit(EXIT_FAILURE);
    }

    Nonterminal top_chunk{chunks[0]};
    Nonterminal bottom_chunk{chunks[1]};

    std::unordered_map<Thread, Transaction> transaction;
    std::unordered_map<std::pair<Nonterminal, TransactionIdx>,
                       std::unordered_set<Event>>
        content;
    std::unordered_map<std::pair<Nonterminal, TransactionIdx>,
                       std::unordered_set<Event>>
        summary;
    std::unordered_map<std::pair<Nonterminal, TransactionIdx>,
                       std::unordered_set<Event>>
        reversed_summary;

    for (const Transaction &vertex : aux_graph[top_chunk].vertices) {
      transaction[vertex.thread] = make_transaction();
      transaction[vertex.thread].thread = vertex.thread;
      content[std::make_pair(top_chunk, transaction[vertex.thread].idx)] =
          vertex.content;
      summary[std::make_pair(top_chunk, transaction[vertex.thread].idx)] =
          vertex.summary;
    }

    for (const Transaction &reversed_vertex :
         aux_graph[bottom_chunk].reversed_vertices) {
      if (transaction.contains(reversed_vertex.thread)) {
        if (transaction[reversed_vertex.thread].thread !=
            reversed_vertex.thread) {
          std::cerr << "Something went wrong\n";
          std::exit(EXIT_FAILURE);
        }
        content[std::make_pair(bottom_chunk,
                               transaction[reversed_vertex.thread].idx)] =
            reversed_vertex.content;
        reversed_summary[std::make_pair(
            bottom_chunk, transaction[reversed_vertex.thread].idx)] =
            reversed_vertex.reversed_summary;
      } else {
        transaction[reversed_vertex.thread] = make_transaction();
        transaction[reversed_vertex.thread].thread = reversed_vertex.thread;
        content[std::make_pair(bottom_chunk,
                               transaction[reversed_vertex.thread].idx)] =
            reversed_vertex.content;
        reversed_summary[std::make_pair(
            bottom_chunk, transaction[reversed_vertex.thread].idx)] =
            reversed_vertex.reversed_summary;
      }
    }

    for (const auto &it : transaction) {
      cross_graph[nonterminal].vertices.insert(it.second);
    }

    for (const auto &edge : aux_graph[top_chunk].edges) {
      cross_graph[nonterminal].edges.insert(std::make_pair(
          transaction[edge.first.thread], transaction[edge.second.thread]));
    }

    for (const auto &edge : aux_graph[bottom_chunk].edges) {
      cross_graph[nonterminal].edges.insert(std::make_pair(
          transaction[edge.first.thread], transaction[edge.second.thread]));
    }

    for (const Transaction &v : cross_graph[nonterminal].vertices) {
      for (const Transaction &w : cross_graph[nonterminal].vertices) {
        if (v != w) {
          for (const Event &e : content[std::make_pair(top_chunk, v.idx)]) {
            for (const Event &f :
                 content[std::make_pair(bottom_chunk, w.idx)]) {
              if (e.conflict(f)) {
                cross_graph[nonterminal].edges.insert(std::make_pair(v, w));
              }
            }
          }
        }

        for (const Event &e : content[std::make_pair(top_chunk, v.idx)]) {
          for (const Event &f :
               reversed_summary[std::make_pair(bottom_chunk, w.idx)]) {
            if (e.conflict(f)) {
              cross_graph[nonterminal].edges.insert(std::make_pair(v, w));
            }
          }
        }

        for (const Event &e : summary[std::make_pair(top_chunk, v.idx)]) {
          for (const Event &f : content[std::make_pair(bottom_chunk, w.idx)]) {
            if (e.conflict(f)) {
              cross_graph[nonterminal].edges.insert(std::make_pair(v, w));
            }
          }
        }

        for (const Event &e : summary[std::make_pair(top_chunk, v.idx)]) {
          for (const Event &f :
               reversed_summary[std::make_pair(bottom_chunk, w.idx)]) {
            if (e.conflict(f)) {
              cross_graph[nonterminal].edges.insert(std::make_pair(v, w));
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
    std::tie(grammar, threads, variables) =
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
