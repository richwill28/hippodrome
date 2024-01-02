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
    std::cout << "Engine: compute_aux_graph (" + A + " -> " + a + ")\n";

    Event event = grammar.content[a];

    if (event.type == EventType::begin) {
      event.annotation = Annotation::down;
    } else if (event.type == EventType::end) {
      event.annotation = Annotation::up;
    } else {
      event.annotation = Annotation::open;
    }

    Transaction txn = make_transaction();
    txn.thread = event.thread;

    if (event.type != EventType::end) {
      aux_graph[A].vertices.insert(txn);
    }

    if (event.type != EventType::begin) {
      aux_graph[A].reversed_vertices.insert(txn);
    }

    aux_graph[A].first_transaction[std::make_tuple(
        event.thread, EventType::undefined, "")] = txn;
    aux_graph[A].first_transaction[std::make_tuple(event.thread, event.type,
                                                   event.operand)] = txn;

    aux_graph[A].last_transaction[std::make_tuple(
        event.thread, EventType::undefined, "")] = txn;
    aux_graph[A].last_transaction[std::make_tuple(event.thread, event.type,
                                                  event.operand)] = txn;

    aux_graph[A].content[txn].insert(event);

    std::cout << aux_graph[A] << "\n";
  }

  bool contains_event(std::unordered_set<Event> set, Event event) {
    for (const Event &it : set) {
      if ((it.thread == event.thread || event.thread == "") &&
          (it.type == event.type || event.type == EventType::undefined) &&
          (it.operand == event.operand || event.operand == "") &&
          (it.annotation == event.annotation ||
           event.annotation == Annotation::undefined)) {
        return true;
      }
    }
    return false;
  }

  void merge_routine(Nonterminal A, Nonterminal B, Nonterminal C,
                     std::unordered_map<Transaction, Transaction> &parent) {

    std::unordered_map<Thread, Transaction> cross_transaction;

    for (const Transaction &vertex : aux_graph[B].vertices) {
      if (vertex.idx == -1) {
        continue;
      }

      Transaction txn = make_transaction();
      txn.thread = vertex.thread;
      aux_graph[B].content[txn] = aux_graph[B].content[vertex];
      aux_graph[A].content[txn] = aux_graph[B].content[vertex];
      cross_transaction[txn.thread] = txn;
      parent[vertex] = txn;
      parent[txn] = txn;
    }

    for (const Transaction &reversed_vertex : aux_graph[C].reversed_vertices) {
      if (reversed_vertex.idx == -1) {
        continue;
      }

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
        parent[txn] = txn;
      }
    }
  }

  void compute_vertices(Nonterminal A, Nonterminal B, Nonterminal C,
                        std::unordered_map<Transaction, Transaction> &parent) {

    for (const Transaction &vertex : aux_graph[C].vertices) {
      if (vertex.idx == -1) {
        continue;
      }

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
      if (vertex.idx == -1) {
        continue;
      }

      if (!contains_event(
              aux_graph[A].content[parent[vertex]],
              Event{"", EventType::end, "", Annotation::undefined})) {
        aux_graph[A].vertices.insert(parent[vertex]);
      }
    }
  }

  void compute_reversed_vertices(
      Nonterminal A, Nonterminal B, Nonterminal C,
      std::unordered_map<Transaction, Transaction> &parent) {

    for (const Transaction &reversed_vertex : aux_graph[B].reversed_vertices) {
      if (reversed_vertex.idx == -1) {
        continue;
      }

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
      if (reversed_vertex.idx == -1) {
        continue;
      }

      if (!contains_event(
              aux_graph[A].content[parent[reversed_vertex]],
              Event{"", EventType::begin, "", Annotation::undefined})) {
        aux_graph[A].reversed_vertices.insert(parent[reversed_vertex]);
      }
    }
  }

  void compute_first_transactions(
      Nonterminal A, Nonterminal B, Nonterminal C,
      std::unordered_map<Transaction, Transaction> &parent) {

    for (const auto &[decor, txn] : aux_graph[C].first_transaction) {
      if (txn.idx == -1) {
        continue;
      }

      if (parent.contains(txn)) {
        aux_graph[A].first_transaction[decor] = parent[txn];
      } else {
        parent[txn] = txn;
        aux_graph[A].first_transaction[decor] = parent[txn];
        aux_graph[A].content[parent[txn]] = aux_graph[C].content[parent[txn]];
      }
    }

    for (const auto &[decor, txn] : aux_graph[B].first_transaction) {
      if (txn.idx == -1) {
        continue;
      }

      if (parent.contains(txn)) {
        aux_graph[A].first_transaction[decor] = parent[txn];
      } else {
        parent[txn] = txn;
        aux_graph[A].first_transaction[decor] = parent[txn];
        aux_graph[A].content[parent[txn]] = aux_graph[B].content[parent[txn]];
      }
    }
  }

  void compute_last_transactions(
      Nonterminal A, Nonterminal B, Nonterminal C,
      std::unordered_map<Transaction, Transaction> &parent) {

    for (const auto &[decor, txn] : aux_graph[B].last_transaction) {
      if (txn.idx == -1) {
        continue;
      }

      if (parent.contains(txn)) {
        aux_graph[A].last_transaction[decor] = parent[txn];
      } else {
        parent[txn] = txn;
        aux_graph[A].last_transaction[decor] = parent[txn];
        aux_graph[A].content[parent[txn]] = aux_graph[B].content[parent[txn]];
      }
    }

    for (const auto &[decor, txn] : aux_graph[C].last_transaction) {
      if (txn.idx == -1) {
        continue;
      }

      if (parent.contains(txn)) {
        aux_graph[A].last_transaction[decor] = parent[txn];
      } else {
        parent[txn] = txn;
        aux_graph[A].last_transaction[decor] = parent[txn];
        aux_graph[A].content[parent[txn]] = aux_graph[C].content[parent[txn]];
      }
    }
  }

  std::unordered_set<Event> compute_neighbor_summary(
      Transaction Ti, Nonterminal A, Nonterminal B,
      const std::unordered_map<Transaction, Transaction> &parent,
      std::unordered_map<std::pair<Nonterminal, Transaction>,
                         std::unordered_set<Event>> &NS) {

    if (NS.contains(std::make_pair(A, Ti))) {
      return NS[std::make_pair(A, Ti)];
    }

    std::cout << "Engine: compute_neighbor_summary(Ti = " + Ti.to_string() +
                     ", A = " + A + ", B = " + B + ")\n";

    std::unordered_set<Event> res;

    for (const auto &edge : aux_graph[B].edges) {
      Transaction Tj = edge.second;
      if (Ti == edge.first &&
          contains_event(
              aux_graph[A].content[parent.at(Tj)],
              Event{"", EventType::end, "", Annotation::undefined})) {
        res.insert(aux_graph[B].content[Tj].begin(),
                   aux_graph[B].content[Tj].end());
        res.insert(aux_graph[B].summary[Tj].begin(),
                   aux_graph[B].summary[Tj].end());
        std::unordered_set<Event> neighbor_summary =
            compute_neighbor_summary(Tj, A, B, parent, NS);
        res.insert(neighbor_summary.begin(), neighbor_summary.end());
      }
    }

    NS[std::make_pair(A, Ti)] = res;

    return res;
  }

  std::unordered_set<Event> compute_trickle_down_summary(
      Transaction Ti, Nonterminal A, Nonterminal B, Nonterminal C,
      const std::unordered_set<Event> &neighbor_summary,
      std::unordered_map<std::pair<Nonterminal, Transaction>,
                         std::unordered_set<Event>> &TDS) {

    if (TDS.contains(std::make_pair(A, Ti))) {
      return TDS[std::make_pair(A, Ti)];
    }

    std::cout << "Engine: compute_trickle_down_summary(Ti = " + Ti.to_string() +
                     ", B = " + B + ", C = " + C + ")\n";

    std::unordered_set<Event> top_summary;
    top_summary.insert(aux_graph[B].content[Ti].begin(),
                       aux_graph[B].content[Ti].end());
    top_summary.insert(aux_graph[B].summary[Ti].begin(),
                       aux_graph[B].summary[Ti].end());
    top_summary.insert(neighbor_summary.begin(), neighbor_summary.end());

    std::vector<std::pair<Event, Event>> args;
    for (const Thread &ui : threads) {
      args.push_back(std::make_pair(
          Event{ui, EventType::undefined, "", Annotation::undefined},
          Event{ui, EventType::undefined, "", Annotation::undefined}));
      args.push_back(std::make_pair(
          Event{ui, EventType::undefined, "", Annotation::undefined},
          Event{"", EventType::fork, ui, Annotation::undefined}));
      for (const Operand &v : variables) {
        args.push_back(std::make_pair(
            Event{ui, EventType::read, v, Annotation::undefined},
            Event{"", EventType::write, v, Annotation::undefined}));
        args.push_back(std::make_pair(
            Event{ui, EventType::write, v, Annotation::undefined},
            Event{"", EventType::write, v, Annotation::undefined}));
        args.push_back(std::make_pair(
            Event{ui, EventType::write, v, Annotation::undefined},
            Event{"", EventType::read, v, Annotation::undefined}));
      }
      for (const Operand &l : locks) {
        args.push_back(std::make_pair(
            Event{ui, EventType::lock, l, Annotation::undefined},
            Event{"", EventType::unlock, l, Annotation::undefined}));
      }
      for (const Thread &uj : threads) {
        args.push_back(std::make_pair(
            Event{ui, EventType::join, uj, Annotation::undefined},
            Event{uj, EventType::undefined, "", Annotation::undefined}));
      }
    }

    std::unordered_set<Event> res;
    for (const std::pair<Event, Event> &arg : args) {
      std::tuple<Thread, EventType, Operand> decor{
          arg.first.thread, arg.first.type, arg.first.operand};
      if (!aux_graph[C].first_transaction.contains(decor)) {
        continue;
      }

      Transaction Tj = aux_graph[C].first_transaction[decor];
      if (Tj.idx == -1) {
        continue;
      }

      if (contains_event(
              aux_graph[C].content[Tj],
              Event{"", EventType::end, "", Annotation::undefined}) &&
          contains_event(top_summary, arg.second)) {
        res.insert(aux_graph[C].content[Tj].begin(),
                   aux_graph[C].content[Tj].end());
        res.insert(aux_graph[C].summary[Tj].begin(),
                   aux_graph[C].summary[Tj].end());
      }
    }

    TDS[std::make_pair(A, Ti)] = res;

    return res;
  }

  std::unordered_set<Event>
  annotate_summary(Transaction Ti, Nonterminal A, Nonterminal B,
                   const std::unordered_set<Event> &summary,
                   std::unordered_map<std::pair<Nonterminal, Transaction>,
                                      std::unordered_set<Event>> &AS) {

    if (AS.contains(std::make_pair(A, Ti))) {
      return AS[std::make_pair(A, Ti)];
    }

    std::unordered_set<Event> res;

    for (Event event : summary) {
      if (event.annotation == Annotation::closed ||
          event.annotation == Annotation::down) {
        res.insert(event);
      } else if (event.annotation == Annotation::up) {
        std::tuple<Thread, EventType, Operand> decor{event.thread,
                                                     EventType::undefined, ""};
        if (!aux_graph[B].last_transaction.contains(decor)) {
          continue;
        }

        Transaction txn = aux_graph[B].last_transaction[decor];
        if (txn.idx == -1) {
          continue;
        }

        if (contains_event(
                aux_graph[B].content[txn],
                Event{"", EventType::begin, "", Annotation::undefined})) {
          res.insert(Event{event.thread, event.type, event.operand,
                           Annotation::closed});
        } else {
          res.insert(event);
        }
      } else if (event.annotation == Annotation::open) {
        std::tuple<Thread, EventType, Operand> decor{event.thread,
                                                     EventType::undefined, ""};
        if (!aux_graph[B].last_transaction.contains(decor)) {
          continue;
        }

        Transaction txn = aux_graph[B].last_transaction[decor];
        if (txn.idx == -1) {
          continue;
        }

        if (contains_event(
                aux_graph[B].content[txn],
                Event{"", EventType::begin, "", Annotation::undefined})) {
          res.insert(
              Event{event.thread, event.type, event.operand, Annotation::down});
        } else {
          res.insert(event);
        }
      } else {
        std::cerr << "Engine: Something went wrong\n";
        std::exit(EXIT_FAILURE);
      }
    }

    AS[std::make_pair(A, Ti)] = res;

    return res;
  }

  std::unordered_set<Event> compute_trickle_up_summary(
      size_t depth, Transaction Ti, Nonterminal A, Nonterminal B, Nonterminal C,
      const std::unordered_map<Transaction, Transaction> &parent,
      std::unordered_map<std::pair<Nonterminal, Transaction>,
                         std::unordered_set<Event>> &NS,
      std::unordered_map<std::pair<Nonterminal, Transaction>,
                         std::unordered_set<Event>> &TDS,
      std::unordered_map<std::pair<Nonterminal, Transaction>,
                         std::unordered_set<Event>> &ATDS) {

    std::cout << "Engine: compute_trickle_up_summary(depth = " << depth
              << ", Ti = " << Ti.to_string() << ", A = " << A << ", B = " << B
              << ", C = " << C << ")\n";

    if (depth >= threads.size()) {
      return std::unordered_set<Event>{};
    }

    std::unordered_set<Event> neighbor_summary =
        compute_neighbor_summary(Ti, A, B, parent, NS);

    std::unordered_set<Event> trickle_down_summary =
        compute_trickle_down_summary(Ti, A, B, C, neighbor_summary, TDS);

    std::unordered_set<Event> annotated_trickle_down_summary =
        annotate_summary(Ti, A, B, trickle_down_summary, ATDS);

    std::unordered_set<Event> trickle_up_summary;
    for (const Event &event : trickle_down_summary) {
      if (Ti.thread != event.thread) {
        if (event.annotation == Annotation::up ||
            event.annotation == Annotation::open) {
          std::tuple<Thread, EventType, Operand> decor{
              event.thread, EventType::undefined, ""};
          if (!aux_graph[B].last_transaction.contains(decor)) {
            continue;
          }

          Transaction Tj = aux_graph[B].last_transaction[decor];
          if (Tj.idx == -1) {
            continue;
          }

          std::unordered_set<Event> summary = compute_trickle_up_summary(
              depth + 1, Tj, A, B, C, parent, NS, TDS, ATDS);
          trickle_up_summary.insert(summary.begin(), summary.end());
        }
      }
    }

    std::unordered_set<Event> res;
    res.insert(aux_graph[B].content[Ti].begin(),
               aux_graph[B].content[Ti].end());
    res.insert(aux_graph[B].summary[Ti].begin(),
               aux_graph[B].summary[Ti].end());
    res.insert(neighbor_summary.begin(), neighbor_summary.end());
    res.insert(annotated_trickle_down_summary.begin(),
               annotated_trickle_down_summary.end());
    res.insert(trickle_up_summary.begin(), trickle_up_summary.end());

    return res;
  }

  void
  compute_summary(Transaction Ti, Nonterminal A, Nonterminal B, Nonterminal C,
                  const std::unordered_map<Transaction, Transaction> &parent,
                  std::unordered_map<std::pair<Nonterminal, Transaction>,
                                     std::unordered_set<Event>> &NS,
                  std::unordered_map<std::pair<Nonterminal, Transaction>,
                                     std::unordered_set<Event>> &TDS,
                  std::unordered_map<std::pair<Nonterminal, Transaction>,
                                     std::unordered_set<Event>> &ATDS,
                  std::unordered_map<std::pair<Nonterminal, Transaction>,
                                     std::unordered_set<Event>> &ACS) {

    std::unordered_set<Event> neighbor_summary;
    if (!contains_event(
            aux_graph[C].content[Ti],
            Event{"", EventType::begin, "", Annotation::undefined})) {
      neighbor_summary = compute_neighbor_summary(Ti, A, B, parent, NS);
    }

    std::unordered_set<Event> trickle_down_summary;
    if (!contains_event(
            aux_graph[C].content[Ti],
            Event{"", EventType::begin, "", Annotation::undefined})) {
      trickle_down_summary =
          compute_trickle_down_summary(Ti, A, B, C, neighbor_summary, TDS);
    }

    std::unordered_set<Event> annotated_trickle_down_summary =
        annotate_summary(Ti, A, B, trickle_down_summary, ATDS);

    std::unordered_set<Event> c_summary = aux_graph[C].summary[Ti];

    std::unordered_set<Event> annotated_c_summary =
        annotate_summary(Ti, C, B, c_summary, ACS);

    std::unordered_set<Event> bottom_summary;
    bottom_summary.insert(trickle_down_summary.begin(),
                          trickle_down_summary.end());
    bottom_summary.insert(c_summary.begin(), c_summary.end());

    std::unordered_set<Event> trickle_up_summary;
    for (const Event &event : bottom_summary) {
      if (Ti.thread != event.thread) {
        if (event.annotation == Annotation::up ||
            event.annotation == Annotation::open) {
          std::tuple<Thread, EventType, Operand> decor{
              event.thread, EventType::undefined, ""};
          if (!aux_graph[B].last_transaction.contains(decor)) {
            continue;
          }

          Transaction Tj = aux_graph[B].last_transaction[decor];
          if (Tj.idx == -1) {
            continue;
          }

          std::unordered_set<Event> summary =
              compute_trickle_up_summary(1, Tj, A, B, C, parent, NS, TDS, ATDS);
          trickle_up_summary.insert(summary.begin(), summary.end());
        }
      }
    }

    if (!contains_event(
            aux_graph[C].content[Ti],
            Event{"", EventType::begin, "", Annotation::undefined})) {
      aux_graph[A].summary[Ti].insert(aux_graph[B].summary[Ti].begin(),
                                      aux_graph[B].summary[Ti].end());
    }
    aux_graph[A].summary[Ti].insert(neighbor_summary.begin(),
                                    neighbor_summary.end());
    aux_graph[A].summary[Ti].insert(annotated_trickle_down_summary.begin(),
                                    annotated_trickle_down_summary.end());
    aux_graph[A].summary[Ti].insert(annotated_c_summary.begin(),
                                    annotated_c_summary.end());
    aux_graph[A].summary[Ti].insert(trickle_up_summary.begin(),
                                    trickle_up_summary.end());
  }

  void compute_summaries(
      Nonterminal A, Nonterminal B, Nonterminal C,
      const std::unordered_map<Transaction, Transaction> &parent) {

    std::unordered_map<std::pair<Nonterminal, Transaction>,
                       std::unordered_set<Event>>
        NS;
    std::unordered_map<std::pair<Nonterminal, Transaction>,
                       std::unordered_set<Event>>
        TDS;
    std::unordered_map<std::pair<Nonterminal, Transaction>,
                       std::unordered_set<Event>>
        ATDS;
    std::unordered_map<std::pair<Nonterminal, Transaction>,
                       std::unordered_set<Event>>
        ACS;

    for (const Transaction &Ti : aux_graph[A].vertices) {
      compute_summary(Ti, A, B, C, parent, NS, TDS, ATDS, ACS);
    }

    for (const auto &[_, Ti] : aux_graph[A].first_transaction) {
      if (!aux_graph[A].summary.contains(Ti)) {
        compute_summary(Ti, A, B, C, parent, NS, TDS, ATDS, ACS);
      }
    }
  }

  void compute_aux_graph_two(Nonterminal A, Nonterminal B, Nonterminal C) {
    std::cout << "Engine: compute_aux_graph (" + A + " -> " + B + " " + C +
                     ")\n";

    std::unordered_map<Transaction, Transaction> parent;

    std::cout << "Before merge_routine\n";
    std::cout << aux_graph[A] << "\n";
    merge_routine(A, B, C, parent);
    std::cout << "After merge_routine\n";
    std::cout << aux_graph[A] << "\n";

    std::cout << "Before compute_vertices\n";
    std::cout << aux_graph[A] << "\n";
    compute_vertices(A, B, C, parent);
    std::cout << "After compute_vertices\n";
    std::cout << aux_graph[A] << "\n";

    std::cout << "Before compute_reversed_vertices\n";
    std::cout << aux_graph[A] << "\n";
    compute_reversed_vertices(A, B, C, parent);
    std::cout << "After compute_reversed_vertices\n";
    std::cout << aux_graph[A] << "\n";

    std::cout << "Before compute_first_transactions\n";
    std::cout << aux_graph[A] << "\n";
    compute_first_transactions(A, B, C, parent);
    std::cout << "After compute_first_transactions\n";
    std::cout << aux_graph[A] << "\n";

    std::cout << "Before compute_last_transactions\n";
    std::cout << aux_graph[A] << "\n";
    compute_last_transactions(A, B, C, parent);
    std::cout << "After compute_last_transactions\n";
    std::cout << aux_graph[A] << "\n";

    std::cout << "Before compute_summaries\n";
    std::cout << aux_graph[A] << "\n";
    compute_summaries(A, B, C, parent);
    std::cout << "After compute_summaries\n";
    std::cout << aux_graph[A] << "\n";
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
      if (vertex.idx == -1) {
        continue;
      }

      cross_transaction[vertex.thread] = make_transaction();
      cross_transaction[vertex.thread].thread = vertex.thread;
      content[std::make_pair(B, cross_transaction[vertex.thread])] =
          aux_graph[B].content[vertex];
      summary[std::make_pair(B, cross_transaction[vertex.thread])] =
          aux_graph[B].summary[vertex];
    }

    for (const Transaction &reversed_vertex : aux_graph[C].reversed_vertices) {
      if (reversed_vertex.idx == -1) {
        continue;
      }

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
    int iteration = 0;
    for (Nonterminal nonterminal : topological_ordering) {
      if (++iteration > 20) {
        break;
      }
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
