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

  TransactionIdx fresh_transaction_idx = 1;

  // For memoization
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
  std::unordered_map<std::pair<Nonterminal, Transaction>,
                     std::unordered_set<Event>>
      TUS;
  std::unordered_map<std::pair<Nonterminal, Transaction>,
                     std::unordered_set<Event>>
      NRS;
  std::unordered_map<std::pair<Nonterminal, Transaction>,
                     std::unordered_set<Event>>
      TDRS;
  std::unordered_map<std::pair<Nonterminal, Transaction>,
                     std::unordered_set<Event>>
      ATDRS;
  std::unordered_map<std::pair<Nonterminal, Transaction>,
                     std::unordered_set<Event>>
      TURS;
  std::unordered_map<std::pair<Nonterminal, Transaction>,
                     std::unordered_set<Event>>
      ABRS;
  std::unordered_map<std::pair<Nonterminal, Transaction>,
                     std::unordered_set<Event>>
      THS;
  std::unordered_map<std::pair<Nonterminal, Transaction>,
                     std::unordered_set<Event>>
      BHRS;

  Transaction make_transaction() {
    return Transaction{fresh_transaction_idx++};
  }

  void compute_aux_graph_one(Nonterminal A, Terminal a) {

#ifdef DEBUG
    std::cout << "Engine: compute_aux_graph (" << A << " -> " << a << ")\n";
#endif

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

#ifdef DEBUG
    std::cout << aux_graph[A] << "\n";
#endif
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
                     std::unordered_map<Transaction, Transaction> &parent,
                     std::unordered_map<std::pair<Nonterminal, Transaction>,
                                        Transaction> &child) {

    std::unordered_map<Thread, Transaction> cross_transaction;

    for (const Transaction &vertex : aux_graph[B].vertices) {
      if (vertex.idx == 0) {
        continue;
      }

      Transaction txn = make_transaction();
      txn.thread = vertex.thread;

      aux_graph[B].content[txn] = aux_graph[B].content[vertex];
      aux_graph[B].summary[txn] = aux_graph[B].summary[vertex];
      aux_graph[B].reversed_summary[txn] =
          aux_graph[B].reversed_summary[vertex];

      aux_graph[A].content[txn] = aux_graph[B].content[vertex];

      parent[vertex] = txn;
      parent[txn] = txn;
      child[std::make_pair(B, txn)] = vertex;
      child[std::make_pair(C, txn)] = Transaction{};

      cross_transaction[txn.thread] = txn;
    }

    for (const Transaction &reversed_vertex : aux_graph[C].reversed_vertices) {
      if (reversed_vertex.idx == 0) {
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
        aux_graph[C].summary[txn] = aux_graph[C].summary[reversed_vertex];
        aux_graph[C].reversed_summary[txn] =
            aux_graph[C].reversed_summary[reversed_vertex];

        Annotation b_annotation = aux_graph[B].content[txn].begin()->annotation;
        Annotation c_annotation = aux_graph[C].content[txn].begin()->annotation;

        Annotation a_annotation = Annotation::undefined;
        if (b_annotation == Annotation::down &&
            c_annotation == Annotation::up) {
          a_annotation = Annotation::closed;
        } else if (b_annotation == Annotation::down &&
                   c_annotation == Annotation::open) {
          a_annotation = Annotation::down;
        } else if (b_annotation == Annotation::open &&
                   c_annotation == Annotation::up) {
          a_annotation = Annotation::up;
        } else if (b_annotation == Annotation::open &&
                   c_annotation == Annotation::open) {
          a_annotation = Annotation::open;
        } else {
          std::cerr << "Engine: Something went wrong\n";
          std::exit(EXIT_FAILURE);
        }

        aux_graph[A].content[txn].clear();

        for (const Event &event : aux_graph[B].content[txn]) {
          aux_graph[A].content[txn].insert(
              Event{event.thread, event.type, event.operand, a_annotation});
        }

        for (const Event &event : aux_graph[C].content[txn]) {
          aux_graph[A].content[txn].insert(
              Event{event.thread, event.type, event.operand, a_annotation});
        }

        parent[reversed_vertex] = txn;
        child[std::make_pair(C, txn)] = reversed_vertex;

      } else {

        Transaction txn = make_transaction();
        txn.thread = reversed_vertex.thread;

        aux_graph[C].content[txn] = aux_graph[C].content[reversed_vertex];
        aux_graph[C].summary[txn] = aux_graph[C].summary[reversed_vertex];
        aux_graph[C].reversed_summary[txn] =
            aux_graph[C].reversed_summary[reversed_vertex];

        aux_graph[A].content[txn] = aux_graph[C].content[reversed_vertex];

        parent[reversed_vertex] = txn;
        parent[txn] = txn;
        child[std::make_pair(B, txn)] = Transaction{};
        child[std::make_pair(C, txn)] = reversed_vertex;

        cross_transaction[txn.thread] = txn;
      }
    }
  }

  void compute_vertices(Nonterminal A, Nonterminal B, Nonterminal C,
                        std::unordered_map<Transaction, Transaction> &parent,
                        std::unordered_map<std::pair<Nonterminal, Transaction>,
                                           Transaction> &child) {

    for (const Transaction &vertex : aux_graph[C].vertices) {
      if (vertex.idx == 0) {
        continue;
      }

      if (parent.contains(vertex)) {
        aux_graph[A].vertices.insert(parent[vertex]);

      } else {

        aux_graph[A].vertices.insert(vertex);
        aux_graph[A].content[vertex] = aux_graph[C].content[vertex];

        parent[vertex] = vertex;
        child[std::make_pair(B, vertex)] = Transaction{};
        child[std::make_pair(C, vertex)] = vertex;
      }
    }

    for (const Transaction &vertex : aux_graph[B].vertices) {
      if (vertex.idx == 0) {
        continue;
      }

      if (!parent.contains(vertex)) {
        std::cerr << "Engine: Something went wrong\n";
        std::exit(EXIT_FAILURE);
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
      std::unordered_map<Transaction, Transaction> &parent,
      std::unordered_map<std::pair<Nonterminal, Transaction>, Transaction>
          &child) {

    for (const Transaction &reversed_vertex : aux_graph[B].reversed_vertices) {
      if (reversed_vertex.idx == 0) {
        continue;
      }

      if (parent.contains(reversed_vertex)) {
        aux_graph[A].reversed_vertices.insert(parent[reversed_vertex]);

      } else {

        aux_graph[A].reversed_vertices.insert(reversed_vertex);
        aux_graph[A].content[reversed_vertex] =
            aux_graph[B].content[reversed_vertex];

        parent[reversed_vertex] = reversed_vertex;
        child[std::make_pair(B, reversed_vertex)] = reversed_vertex;
        child[std::make_pair(C, reversed_vertex)] = Transaction{};
      }
    }

    for (const Transaction &reversed_vertex : aux_graph[C].reversed_vertices) {
      if (reversed_vertex.idx == 0) {
        continue;
      }

      if (!parent.contains(reversed_vertex)) {
        std::cerr << "Engine: Something went wrong\n";
        std::exit(EXIT_FAILURE);
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
      std::unordered_map<Transaction, Transaction> &parent,
      std::unordered_map<std::pair<Nonterminal, Transaction>, Transaction>
          &child) {

    for (const auto &[decor, txn] : aux_graph[C].first_transaction) {
      if (txn.idx == 0) {
        continue;
      }

      if (parent.contains(txn)) {
        aux_graph[A].first_transaction[decor] = parent[txn];

      } else {
        aux_graph[A].first_transaction[decor] = txn;
        aux_graph[A].content[parent[txn]] = aux_graph[C].content[txn];

        parent[txn] = txn;
        child[std::make_pair(B, txn)] = Transaction{};
        child[std::make_pair(C, txn)] = txn;
      }
    }

    for (const auto &[decor, txn] : aux_graph[B].first_transaction) {
      if (txn.idx == 0) {
        continue;
      }

      if (parent.contains(txn)) {
        aux_graph[A].first_transaction[decor] = parent[txn];

      } else {
        aux_graph[A].first_transaction[decor] = txn;
        aux_graph[A].content[parent[txn]] = aux_graph[B].content[txn];

        parent[txn] = txn;
        child[std::make_pair(B, txn)] = txn;
        child[std::make_pair(C, txn)] = Transaction{};
      }
    }
  }

  void compute_last_transactions(
      Nonterminal A, Nonterminal B, Nonterminal C,
      std::unordered_map<Transaction, Transaction> &parent,
      std::unordered_map<std::pair<Nonterminal, Transaction>, Transaction>
          &child) {

    for (const auto &[decor, txn] : aux_graph[B].last_transaction) {
      if (txn.idx == 0) {
        continue;
      }

      if (parent.contains(txn)) {
        aux_graph[A].last_transaction[decor] = parent[txn];

      } else {
        aux_graph[A].last_transaction[decor] = txn;
        aux_graph[A].content[parent[txn]] = aux_graph[B].content[txn];

        parent[txn] = txn;
        child[std::make_pair(B, txn)] = txn;
        child[std::make_pair(C, txn)] = Transaction{};
      }
    }

    for (const auto &[decor, txn] : aux_graph[C].last_transaction) {
      if (txn.idx == 0) {
        continue;
      }

      if (parent.contains(txn)) {
        aux_graph[A].last_transaction[decor] = parent[txn];

      } else {
        aux_graph[A].last_transaction[decor] = txn;
        aux_graph[A].content[parent[txn]] = aux_graph[C].content[txn];

        parent[txn] = txn;
        child[std::make_pair(B, txn)] = Transaction{};
        child[std::make_pair(C, txn)] = txn;
      }
    }
  }

  std::unordered_set<Event> compute_neighbor_summary_fixpoint(
      Transaction Ti, Nonterminal A, Nonterminal B,
      const std::unordered_map<Transaction, Transaction> &parent,
      std::unordered_set<Thread> &avoid) {

    if (avoid.contains(Ti.thread)) {
      return std::unordered_set<Event>{};
    }

    avoid.insert(Ti.thread);

    std::unordered_set<Event> res;

    for (const auto &edge : aux_graph[B].edges) {
      Transaction Tj = edge.second;

      if (Ti == edge.first &&
          contains_event(
              aux_graph[A].content[parent.at(Tj)],
              Event{"", EventType::end, "", Annotation::undefined})) {

        for (const Event &event : aux_graph[B].content[Tj]) {

          Annotation annotation = Annotation::undefined;
          if (event.annotation == Annotation::open ||
              event.annotation == Annotation::up) {
            annotation = Annotation::up;
          } else if (event.annotation == Annotation::down ||
                     event.annotation == Annotation::closed) {
            annotation = Annotation::closed;
          } else {
            std::cerr << "Engine: Something went wrong\n";
            std::exit(EXIT_FAILURE);
          }

          res.insert(
              Event{event.thread, event.type, event.operand, annotation});
        }

        res.insert(aux_graph[B].summary[Tj].begin(),
                   aux_graph[B].summary[Tj].end());

        std::unordered_set<Event> neighbor_summary =
            compute_neighbor_summary_fixpoint(Tj, A, B, parent, avoid);

        res.insert(neighbor_summary.begin(), neighbor_summary.end());
      }
    }

    return res;
  }

  std::unordered_set<Event> compute_neighbor_summary(
      Transaction Ti, Nonterminal A, Nonterminal B,
      const std::unordered_map<Transaction, Transaction> &parent) {

    std::pair<Nonterminal, Transaction> decor{A, Ti};

    if (NS.contains(decor)) {
      return NS[decor];
    }

    std::unordered_set<Thread> avoid;
    NS[decor] = compute_neighbor_summary_fixpoint(Ti, A, B, parent, avoid);

    return NS[decor];
  }

  std::unordered_set<Event> compute_trickle_down_summary(
      Transaction Ti, Nonterminal A, Nonterminal B, Nonterminal C,
      const std::unordered_set<Event> &neighbor_summary,
      const std::unordered_map<Transaction, Transaction> &parent) {

    if (TDS.contains(std::make_pair(A, Ti))) {
      return TDS[std::make_pair(A, Ti)];
    }

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
      if (Tj.idx == 0) {
        continue;
      }

      if (parent.at(Ti) == parent.at(Tj)) {
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

    for (const Event &event : summary) {

      if (event.annotation == Annotation::closed ||
          event.annotation == Annotation::down) {

        res.insert(
            Event{event.thread, event.type, event.operand, event.annotation});

      } else if (event.annotation == Annotation::up) {

        std::tuple<Thread, EventType, Operand> decor{event.thread,
                                                     EventType::undefined, ""};

        if (!aux_graph[B].last_transaction.contains(decor)) {
          res.insert(
              Event{event.thread, event.type, event.operand, event.annotation});
          continue;
        }

        Transaction txn = aux_graph[B].last_transaction[decor];

        Annotation annotation = event.annotation;
        if (contains_event(
                aux_graph[B].content[txn],
                Event{"", EventType::begin, "", Annotation::undefined})) {
          annotation = Annotation::closed;
        }

        res.insert(Event{event.thread, event.type, event.operand, annotation});

      } else if (event.annotation == Annotation::open) {

        std::tuple<Thread, EventType, Operand> decor{event.thread,
                                                     EventType::undefined, ""};

        if (!aux_graph[B].last_transaction.contains(decor)) {
          res.insert(
              Event{event.thread, event.type, event.operand, event.annotation});
          continue;
        }

        Transaction txn = aux_graph[B].last_transaction[decor];

        Annotation annotation = event.annotation;
        if (contains_event(
                aux_graph[B].content[txn],
                Event{"", EventType::begin, "", Annotation::undefined})) {
          annotation = Annotation::down;
        }

        res.insert(Event{event.thread, event.type, event.operand, annotation});

      } else {
        std::cerr << "Engine: Something went wrong\n";
        std::exit(EXIT_FAILURE);
      }
    }

    AS[std::make_pair(A, Ti)] = res;

    return res;
  }

  std::unordered_set<Event> compute_trickle_up_summary_fixpoint(
      Transaction Ti, Transaction Tj, Nonterminal A, Nonterminal B,
      Nonterminal C, const std::unordered_map<Transaction, Transaction> &parent,
      std::unordered_set<Thread> &avoid) {

    if (avoid.contains(Tj.thread)) {
      return std::unordered_set<Event>{};
    }

    avoid.insert(Tj.thread);

    std::unordered_set<Event> neighbor_summary =
        compute_neighbor_summary(Tj, A, B, parent);

    std::unordered_set<Event> trickle_down_summary =
        compute_trickle_down_summary(Tj, A, B, C, neighbor_summary, parent);

    std::unordered_set<Event> annotated_trickle_down_summary =
        annotate_summary(Tj, A, B, trickle_down_summary, ATDS);

    std::unordered_set<Event> trickle_up_summary;

    for (const Event &event : trickle_down_summary) {
      if (avoid.contains(event.thread)) {
        continue;
      }

      if (event.annotation == Annotation::up ||
          event.annotation == Annotation::open) {

        std::tuple<Thread, EventType, Operand> decor{event.thread,
                                                     EventType::undefined, ""};

        if (!aux_graph[B].last_transaction.contains(decor)) {
          continue;
        }

        Transaction Tk = aux_graph[B].last_transaction[decor];
        if (Tk.idx == 0) {
          continue;
        }

        std::unordered_set<Event> res =
            compute_trickle_up_summary_fixpoint(Ti, Tk, A, B, C, parent, avoid);
        trickle_up_summary.insert(res.begin(), res.end());
      }
    }

    std::unordered_set<Event> res;

    res.insert(aux_graph[B].content[Tj].begin(),
               aux_graph[B].content[Tj].end());
    res.insert(aux_graph[B].summary[Tj].begin(),
               aux_graph[B].summary[Tj].end());
    res.insert(neighbor_summary.begin(), neighbor_summary.end());
    res.insert(annotated_trickle_down_summary.begin(),
               annotated_trickle_down_summary.end());
    res.insert(trickle_up_summary.begin(), trickle_up_summary.end());

    THS[std::make_pair(A, Ti)].insert(aux_graph[B].content[Tj].begin(),
                                      aux_graph[B].content[Tj].end());
    THS[std::make_pair(A, Ti)].insert(aux_graph[B].summary[Tj].begin(),
                                      aux_graph[B].summary[Tj].end());
    THS[std::make_pair(A, Ti)].insert(neighbor_summary.begin(),
                                      neighbor_summary.end());

    return res;
  }

  std::unordered_set<Event> compute_trickle_up_summary(
      Transaction Ti, Nonterminal A, Nonterminal B, Nonterminal C,
      const std::unordered_map<Transaction, Transaction> &parent) {

    std::pair<Nonterminal, Transaction> decor{A, Ti};

    if (TUS.contains(decor)) {
      return TUS[decor];
    }

    std::unordered_set<Thread> avoid;
    TUS[decor] =
        compute_trickle_up_summary_fixpoint(Ti, Ti, A, B, C, parent, avoid);

    return TUS[decor];
  }

  void
  compute_summary(Transaction Ti, Nonterminal A, Nonterminal B, Nonterminal C,
                  const std::unordered_map<Transaction, Transaction> &parent) {

    if (aux_graph[A].summary.contains(Ti)) {
      return;
    }

#ifdef DEBUG
    std::cout << "Engine: compute_summary(Ti = " << Ti << ", A = " << A
              << ", B = " << B << ", C = " << C << ")\n";
#endif

    std::unordered_set<Event> neighbor_summary;

    if (!contains_event(
            aux_graph[C].content[Ti],
            Event{"", EventType::begin, "", Annotation::undefined})) {
      neighbor_summary = compute_neighbor_summary(Ti, A, B, parent);
    }

#ifdef DEBUG
    std::cout << "NS =";
    for (const Event &event : neighbor_summary) {
      std::cout << " " << event;
    }
    std::cout << "\n";
#endif

    std::unordered_set<Event> trickle_down_summary;
    if (!contains_event(
            aux_graph[C].content[Ti],
            Event{"", EventType::begin, "", Annotation::undefined})) {
      trickle_down_summary =
          compute_trickle_down_summary(Ti, A, B, C, neighbor_summary, parent);
    }

#ifdef DEBUG
    std::cout << "TDS =";
    for (const Event &event : trickle_down_summary) {
      std::cout << " " << event;
    }
    std::cout << "\n";
#endif

    std::unordered_set<Event> annotated_trickle_down_summary =
        annotate_summary(Ti, A, B, trickle_down_summary, ATDS);

#ifdef DEBUG
    std::cout << "ATDS =";
    for (const Event &event : annotated_trickle_down_summary) {
      std::cout << " " << event;
    }
    std::cout << "\n";
#endif

    std::unordered_set<Event> c_summary = aux_graph[C].summary[Ti];

    std::unordered_set<Event> annotated_c_summary =
        annotate_summary(Ti, C, B, c_summary, ACS);

    std::unordered_set<Event> bottom_summary;
    bottom_summary.insert(trickle_down_summary.begin(),
                          trickle_down_summary.end());
    bottom_summary.insert(c_summary.begin(), c_summary.end());

    std::unordered_set<Event> trickle_up_summary;

    for (const Event &event : bottom_summary) {
      if (event.thread == Ti.thread) {
        continue;
      }

      if (event.annotation == Annotation::up ||
          event.annotation == Annotation::open) {

        std::tuple<Thread, EventType, Operand> decor{event.thread,
                                                     EventType::undefined, ""};

        if (!aux_graph[B].last_transaction.contains(decor)) {
          continue;
        }

        Transaction Tj = aux_graph[B].last_transaction[decor];
        if (Tj.idx == 0) {
          continue;
        }

        std::unordered_set<Event> res =
            compute_trickle_up_summary(Tj, A, B, C, parent);
        trickle_up_summary.insert(res.begin(), res.end());
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

    if (!contains_event(
            aux_graph[C].content[Ti],
            Event{"", EventType::begin, "", Annotation::undefined})) {
      THS[std::make_pair(A, Ti)].insert(aux_graph[B].summary[Ti].begin(),
                                        aux_graph[B].summary[Ti].end());
    }
    THS[std::make_pair(A, Ti)].insert(neighbor_summary.begin(),
                                      neighbor_summary.end());
  }

  void compute_summaries(
      Nonterminal A, Nonterminal B, Nonterminal C,
      const std::unordered_map<Transaction, Transaction> &parent) {

    for (const Transaction &Ti : aux_graph[A].vertices) {
      compute_summary(Ti, A, B, C, parent);
    }

    for (const auto &[_, Ti] : aux_graph[A].first_transaction) {
      compute_summary(Ti, A, B, C, parent);
    }
  }

  std::unordered_set<Event> compute_neighbor_reversed_summary_fixpoint(
      Transaction Ti, Nonterminal A, Nonterminal C,
      const std::unordered_map<Transaction, Transaction> &parent,
      std::unordered_set<Thread> &avoid) {

    if (avoid.contains(Ti.thread)) {
      return std::unordered_set<Event>{};
    }

    avoid.insert(Ti.thread);

    std::unordered_set<Event> res;

    for (const auto &edge : aux_graph[C].edges) {
      Transaction Tj = edge.first;

      if (Ti == edge.second &&
          contains_event(
              aux_graph[A].content[parent.at(Tj)],
              Event{"", EventType::begin, "", Annotation::undefined})) {

        for (const Event &event : aux_graph[C].content[Tj]) {

          Annotation annotation = Annotation::undefined;
          if (event.annotation == Annotation::open ||
              event.annotation == Annotation::down) {
            annotation = Annotation::down;
          } else if (event.annotation == Annotation::up ||
                     event.annotation == Annotation::closed) {
            annotation = Annotation::closed;
          } else {
            std::cerr << "Engine: Something went wrong\n";
            std::exit(EXIT_FAILURE);
          }

          res.insert(
              Event{event.thread, event.type, event.operand, annotation});
        }

        res.insert(aux_graph[C].reversed_summary[Tj].begin(),
                   aux_graph[C].reversed_summary[Tj].end());

        std::unordered_set<Event> neighbor_reversed_summary =
            compute_neighbor_reversed_summary_fixpoint(Tj, A, C, parent, avoid);

        res.insert(neighbor_reversed_summary.begin(),
                   neighbor_reversed_summary.end());
      }
    }

    return res;
  }

  std::unordered_set<Event> compute_neighbor_reversed_summary(
      Transaction Ti, Nonterminal A, Nonterminal C,
      const std::unordered_map<Transaction, Transaction> &parent) {

    std::pair<Nonterminal, Transaction> decor{A, Ti};

    if (NRS.contains(decor)) {
      return NRS[decor];
    }

    std::unordered_set<Thread> avoid;
    NRS[decor] =
        compute_neighbor_reversed_summary_fixpoint(Ti, A, C, parent, avoid);

    return NRS[decor];
  }

  std::unordered_set<Event> compute_trickle_down_reversed_summary(
      Transaction Ti, Nonterminal A, Nonterminal B, Nonterminal C,
      const std::unordered_set<Event> &neighbor_reversed_summary,
      const std::unordered_map<Transaction, Transaction> &parent) {

    if (TDRS.contains(std::make_pair(A, Ti))) {
      return TDRS[std::make_pair(A, Ti)];
    }

#ifdef DEBUG
    std::cout << "Engine: compute_trickle_down_reversed_summary(Ti = " << Ti
              << ", A = " << A << ", B = " << B << ", C = " << C << ")\n";
#endif

    std::unordered_set<Event> bottom_reversed_summary;
    bottom_reversed_summary.insert(aux_graph[C].content[Ti].begin(),
                                   aux_graph[C].content[Ti].end());
    bottom_reversed_summary.insert(aux_graph[C].summary[Ti].begin(),
                                   aux_graph[C].summary[Ti].end());
    bottom_reversed_summary.insert(neighbor_reversed_summary.begin(),
                                   neighbor_reversed_summary.end());

    std::vector<std::pair<Event, Event>> args;
    for (const Thread &ui : threads) {
      args.push_back(std::make_pair(
          Event{ui, EventType::undefined, "", Annotation::undefined},
          Event{ui, EventType::undefined, "", Annotation::undefined}));
      args.push_back(std::make_pair(
          Event{ui, EventType::undefined, "", Annotation::undefined},
          Event{"", EventType::join, ui, Annotation::undefined}));
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
            Event{ui, EventType::unlock, l, Annotation::undefined},
            Event{"", EventType::lock, l, Annotation::undefined}));
      }
      for (const Thread &uj : threads) {
        args.push_back(std::make_pair(
            Event{ui, EventType::fork, uj, Annotation::undefined},
            Event{uj, EventType::undefined, "", Annotation::undefined}));
      }
    }

    std::unordered_set<Event> res;
    for (const std::pair<Event, Event> &arg : args) {

      std::tuple<Thread, EventType, Operand> decor{
          arg.first.thread, arg.first.type, arg.first.operand};

      if (!aux_graph[B].last_transaction.contains(decor)) {
        continue;
      }

      Transaction Tj = aux_graph[B].last_transaction[decor];
      if (Tj.idx == 0) {
        continue;
      }

      if (parent.at(Ti) == parent.at(Tj)) {
        continue;
      }

      if (contains_event(
              aux_graph[B].content[Tj],
              Event{"", EventType::begin, "", Annotation::undefined}) &&
          contains_event(bottom_reversed_summary, arg.second)) {
        res.insert(aux_graph[B].content[Tj].begin(),
                   aux_graph[B].content[Tj].end());
        res.insert(aux_graph[B].reversed_summary[Tj].begin(),
                   aux_graph[B].reversed_summary[Tj].end());
      }
    }

    TDRS[std::make_pair(A, Ti)] = res;

    return res;
  }

  std::unordered_set<Event> annotate_reversed_summary(
      Transaction Ti, Nonterminal A, Nonterminal C,
      const std::unordered_set<Event> &reversed_summary,
      std::unordered_map<std::pair<Nonterminal, Transaction>,
                         std::unordered_set<Event>> &ARS) {

    if (ARS.contains(std::make_pair(A, Ti))) {
      return ARS[std::make_pair(A, Ti)];
    }

    std::unordered_set<Event> res;

    for (const Event &event : reversed_summary) {

      if (event.annotation == Annotation::closed ||
          event.annotation == Annotation::up) {

        res.insert(
            Event{event.thread, event.type, event.operand, event.annotation});

      } else if (event.annotation == Annotation::down) {

        std::tuple<Thread, EventType, Operand> decor{event.thread,
                                                     EventType::undefined, ""};

        if (!aux_graph[C].first_transaction.contains(decor)) {
          res.insert(
              Event{event.thread, event.type, event.operand, event.annotation});
          continue;
        }

        Transaction txn = aux_graph[C].first_transaction[decor];

        Annotation annotation = event.annotation;
        if (contains_event(
                aux_graph[C].content[txn],
                Event{"", EventType::end, "", Annotation::undefined})) {
          annotation = Annotation::closed;
        }

        res.insert(Event{event.thread, event.type, event.operand, annotation});

      } else if (event.annotation == Annotation::open) {

        std::tuple<Thread, EventType, Operand> decor{event.thread,
                                                     EventType::undefined, ""};

        if (!aux_graph[C].first_transaction.contains(decor)) {
          res.insert(
              Event{event.thread, event.type, event.operand, event.annotation});
          continue;
        }

        Transaction txn = aux_graph[C].first_transaction[decor];

        Annotation annotation = event.annotation;
        if (contains_event(
                aux_graph[C].content[txn],
                Event{"", EventType::end, "", Annotation::undefined})) {
          annotation = Annotation::up;
        }

        res.insert(Event{event.thread, event.type, event.operand, annotation});

      } else {
        std::cerr << "Engine: Something went wrong\n";
        std::exit(EXIT_FAILURE);
      }
    }

    ARS[std::make_pair(A, Ti)] = res;

    return res;
  }

  std::unordered_set<Event> compute_trickle_up_reversed_summary_fixpoint(
      Transaction Ti, Transaction Tj, Nonterminal A, Nonterminal B,
      Nonterminal C, const std::unordered_map<Transaction, Transaction> &parent,
      std::unordered_set<Thread> &avoid) {

    if (avoid.contains(Tj.thread)) {
      return std::unordered_set<Event>{};
    }

    avoid.insert(Tj.thread);

    std::unordered_set<Event> neighbor_reversed_summary =
        compute_neighbor_reversed_summary(Tj, A, C, parent);

    std::unordered_set<Event> trickle_down_reversed_summary =
        compute_trickle_down_reversed_summary(
            Tj, A, B, C, neighbor_reversed_summary, parent);

    std::unordered_set<Event> annotated_trickle_down_reversed_summary =
        annotate_reversed_summary(Tj, A, C, trickle_down_reversed_summary,
                                  ATDRS);

    std::unordered_set<Event> trickle_up_reversed_summary;

    for (const Event &event : trickle_down_reversed_summary) {
      if (avoid.contains(event.thread)) {
        continue;
      }

      if (event.annotation == Annotation::down ||
          event.annotation == Annotation::open) {

        std::tuple<Thread, EventType, Operand> decor{event.thread,
                                                     EventType::undefined, ""};

        if (!aux_graph[C].first_transaction.contains(decor)) {
          continue;
        }

        Transaction Tk = aux_graph[C].first_transaction[decor];
        if (Tk.idx == 0) {
          continue;
        }

        std::unordered_set<Event> res =
            compute_trickle_up_reversed_summary_fixpoint(Ti, Tk, A, B, C,
                                                         parent, avoid);
        trickle_up_reversed_summary.insert(res.begin(), res.end());
      }
    }

    std::unordered_set<Event> res;

    res.insert(aux_graph[C].content[Tj].begin(),
               aux_graph[C].content[Tj].end());
    res.insert(aux_graph[C].reversed_summary[Tj].begin(),
               aux_graph[C].reversed_summary[Tj].end());
    res.insert(neighbor_reversed_summary.begin(),
               neighbor_reversed_summary.end());
    res.insert(annotated_trickle_down_reversed_summary.begin(),
               annotated_trickle_down_reversed_summary.end());
    res.insert(trickle_up_reversed_summary.begin(),
               trickle_up_reversed_summary.end());

    BHRS[std::make_pair(A, Ti)].insert(aux_graph[C].content[Tj].begin(),
                                       aux_graph[C].content[Tj].end());
    BHRS[std::make_pair(A, Ti)].insert(
        aux_graph[C].reversed_summary[Tj].begin(),
        aux_graph[C].reversed_summary[Tj].end());
    BHRS[std::make_pair(A, Ti)].insert(neighbor_reversed_summary.begin(),
                                       neighbor_reversed_summary.end());

    return res;
  }

  std::unordered_set<Event> compute_trickle_up_reversed_summary(
      Transaction Ti, Nonterminal A, Nonterminal B, Nonterminal C,
      const std::unordered_map<Transaction, Transaction> &parent) {

    if (TURS.contains(std::make_pair(A, Ti))) {
      return TURS[std::make_pair(A, Ti)];
    }

    std::unordered_set<Thread> avoid;
    TURS[std::make_pair(A, Ti)] = compute_trickle_up_reversed_summary_fixpoint(
        Ti, Ti, A, B, C, parent, avoid);

    return TURS[std::make_pair(A, Ti)];
  }

  void compute_reversed_summary(
      Transaction Ti, Nonterminal A, Nonterminal B, Nonterminal C,
      const std::unordered_map<Transaction, Transaction> &parent) {

    if (aux_graph[A].reversed_summary.contains(Ti)) {
      return;
    }

    std::unordered_set<Event> neighbor_reversed_summary;
    if (!contains_event(aux_graph[B].content[Ti],
                        Event{"", EventType::end, "", Annotation::undefined})) {
      neighbor_reversed_summary =
          compute_neighbor_reversed_summary(Ti, A, C, parent);
    }

    std::unordered_set<Event> trickle_down_reversed_summary;
    if (!contains_event(aux_graph[B].content[Ti],
                        Event{"", EventType::end, "", Annotation::undefined})) {
      trickle_down_reversed_summary = compute_trickle_down_reversed_summary(
          Ti, A, B, C, neighbor_reversed_summary, parent);
    }

    std::unordered_set<Event> annotated_trickle_down_reversed_summary =
        annotate_reversed_summary(Ti, A, C, trickle_down_reversed_summary,
                                  ATDRS);

    std::unordered_set<Event> b_reversed_summary = aux_graph[B].summary[Ti];

    std::unordered_set<Event> annotated_b_reversed_summary =
        annotate_reversed_summary(Ti, B, C, b_reversed_summary, ABRS);

    std::unordered_set<Event> top_reversed_summary;
    top_reversed_summary.insert(trickle_down_reversed_summary.begin(),
                                trickle_down_reversed_summary.end());
    top_reversed_summary.insert(b_reversed_summary.begin(),
                                b_reversed_summary.end());

    std::unordered_set<Event> trickle_up_reversed_summary;

    for (const Event &event : top_reversed_summary) {
      if (event.thread == Ti.thread) {
        continue;
      }

      if (event.annotation == Annotation::down ||
          event.annotation == Annotation::open) {

        std::tuple<Thread, EventType, Operand> decor{event.thread,
                                                     EventType::undefined, ""};

        if (!aux_graph[C].first_transaction.contains(decor)) {
          continue;
        }

        Transaction Tj = aux_graph[C].first_transaction[decor];
        if (Tj.idx == 0) {
          continue;
        }

        std::unordered_set<Event> res =
            compute_trickle_up_reversed_summary(Tj, A, B, C, parent);
        trickle_up_reversed_summary.insert(res.begin(), res.end());
      }
    }

    if (!contains_event(aux_graph[B].content[Ti],
                        Event{"", EventType::end, "", Annotation::undefined})) {
      aux_graph[A].reversed_summary[Ti].insert(
          aux_graph[C].reversed_summary[Ti].begin(),
          aux_graph[C].reversed_summary[Ti].end());
    }
    aux_graph[A].reversed_summary[Ti].insert(neighbor_reversed_summary.begin(),
                                             neighbor_reversed_summary.end());
    aux_graph[A].reversed_summary[Ti].insert(
        annotated_trickle_down_reversed_summary.begin(),
        annotated_trickle_down_reversed_summary.end());
    aux_graph[A].reversed_summary[Ti].insert(
        annotated_b_reversed_summary.begin(),
        annotated_b_reversed_summary.end());
    aux_graph[A].reversed_summary[Ti].insert(
        trickle_up_reversed_summary.begin(), trickle_up_reversed_summary.end());

    if (!contains_event(aux_graph[B].content[Ti],
                        Event{"", EventType::end, "", Annotation::undefined})) {
      BHRS[std::make_pair(A, Ti)].insert(
          aux_graph[C].reversed_summary[Ti].begin(),
          aux_graph[C].reversed_summary[Ti].end());
    }
    BHRS[std::make_pair(A, Ti)].insert(neighbor_reversed_summary.begin(),
                                       neighbor_reversed_summary.end());
  }

  void compute_reversed_summaries(
      Nonterminal A, Nonterminal B, Nonterminal C,
      const std::unordered_map<Transaction, Transaction> &parent) {

    for (const Transaction &Ti : aux_graph[A].reversed_vertices) {
      compute_reversed_summary(Ti, A, B, C, parent);
    }

    for (const auto &[_, Ti] : aux_graph[A].last_transaction) {
      compute_reversed_summary(Ti, A, B, C, parent);
    }
  }

  void compute_edge(
      Transaction Ti, Transaction Tj, Nonterminal A, Nonterminal B,
      Nonterminal C, const std::unordered_map<Transaction, Transaction> &parent,
      const std::unordered_map<std::pair<Nonterminal, Transaction>, Transaction>
          &child) {

    if (aux_graph[C].edges.contains(
            std::make_pair(child.at(std::make_pair(C, Ti)),
                           child.at(std::make_pair(C, Tj))))) {
      aux_graph[A].edges.insert(std::make_pair(Ti, Tj));
      return;
    }

    if (aux_graph[B].edges.contains(
            std::make_pair(child.at(std::make_pair(B, Ti)),
                           child.at(std::make_pair(B, Tj))))) {
      aux_graph[A].edges.insert(std::make_pair(Ti, Tj));
      return;
    }

    if (Ti != Tj) {
      for (const Event &ei : aux_graph[B].content[Ti]) {
        for (const Event &ej : aux_graph[C].content[Tj]) {
          if (ei.conflict(ej)) {
            aux_graph[A].edges.insert(std::make_pair(Ti, Tj));
            return;
          }
        }
      }
    }

    for (const Event &ei : aux_graph[B].content[Ti]) {
      for (const auto &[_, Tk] : aux_graph[C].first_transaction) {
        if (Ti != parent.at(Tk) &&
            contains_event(
                aux_graph[C].content[Tk],
                Event{"", EventType::end, "", Annotation::undefined})) {
          for (const Event &ek : aux_graph[C].content[Tk]) {
            if (ei.conflict(ek) &&
                (Tk.thread == Tj.thread ||
                 aux_graph[C].reachable(Tk, child.at(std::make_pair(C, Tj)),
                                        aux_graph[C].vertices))) {
              aux_graph[A].edges.insert(std::make_pair(Ti, Tj));
              return;
            }
          }
        }
      }
    }

    for (const Event &ei : THS[std::make_pair(A, Ti)]) {
      for (const Event &ej : aux_graph[C].content[Tj]) {
        if (ei.conflict(ej)) {
          aux_graph[A].edges.insert(std::make_pair(Ti, Tj));
          return;
        }
      }
    }

    for (const Event &ei : THS[std::make_pair(A, Ti)]) {
      for (const auto &[_, Tk] : aux_graph[C].first_transaction) {
        if (contains_event(
                aux_graph[C].content[Tk],
                Event{"", EventType::end, "", Annotation::undefined})) {
          for (const Event &ek : aux_graph[C].content[Tk]) {
            if (ei.conflict(ek) &&
                (Tk.thread == Tj.thread ||
                 aux_graph[C].reachable(Tk, child.at(std::make_pair(C, Tj)),
                                        aux_graph[C].vertices))) {
              aux_graph[A].edges.insert(std::make_pair(Ti, Tj));
              return;
            }
          }
        }
      }
    }

    for (const Event &ei : THS[std::make_pair(A, Ti)]) {
      Transaction Tk = aux_graph[B].last_transaction[std::make_tuple(
          ei.thread, EventType::undefined, "")];
      if (contains_event(
              aux_graph[A].content[parent.at(Tk)],
              Event{"", EventType::end, "", Annotation::undefined})) {
        if (Tk.thread == Tj.thread ||
            aux_graph[B].edges.contains(
                std::make_pair(Tk, child.at(std::make_pair(B, Tj))))) {
          aux_graph[A].edges.insert(std::make_pair(Ti, Tj));
          return;
        }
      }
    }

    // for (const Event &ei : THS[std::make_pair(A, Ti)]) {
    //   for (const auto &[_, Tk] : aux_graph[C].first_transaction) {
    //     if (contains_event(
    //             aux_graph[C].content[Tk],
    //             Event{"", EventType::end, "", Annotation::undefined})) {
    //       for (const Event &ek : aux_graph[C].content[Tk]) {
    //         if (ei.conflict(ek) &&
    //             (Tk.thread == Tj.thread ||
    //              aux_graph[B].edges.contains(
    //                  std::make_pair(child.at(std::make_pair(B,
    //                  parent.at(Tk))),
    //                                 child.at(std::make_pair(B, Tj)))))) {
    //           aux_graph[A].edges.insert(std::make_pair(Ti, Tj));
    //           return;
    //         }
    //       }
    //     }
    //   }
    // }
  }

  void compute_edges(
      Nonterminal A, Nonterminal B, Nonterminal C,
      const std::unordered_map<Transaction, Transaction> &parent,
      const std::unordered_map<std::pair<Nonterminal, Transaction>, Transaction>
          &child) {

    for (const Transaction &Ti : aux_graph[A].vertices) {
      for (const Transaction &Tj : aux_graph[A].vertices) {
        compute_edge(Ti, Tj, A, B, C, parent, child);
      }
    }

    for (const auto &[_, Ti] : aux_graph[A].first_transaction) {
      if (aux_graph[A].vertices.contains(Ti)) {
        continue;
      }
      for (const Transaction &Tj : aux_graph[A].vertices) {
        compute_edge(Ti, Tj, A, B, C, parent, child);
      }
    }
  }

  void compute_reversed_edge(
      Transaction Tj, Transaction Ti, Nonterminal A, Nonterminal B,
      Nonterminal C, const std::unordered_map<Transaction, Transaction> &parent,
      const std::unordered_map<std::pair<Nonterminal, Transaction>, Transaction>
          &child) {

    if (aux_graph[B].edges.contains(
            std::make_pair(child.at(std::make_pair(B, Tj)),
                           child.at(std::make_pair(B, Ti))))) {
      aux_graph[A].edges.insert(std::make_pair(Tj, Ti));
      return;
    }

    if (aux_graph[C].edges.contains(
            std::make_pair(child.at(std::make_pair(C, Tj)),
                           child.at(std::make_pair(C, Ti))))) {
      aux_graph[A].edges.insert(std::make_pair(Tj, Ti));
      return;
    }

    if (Ti != Tj) {
      for (const Event &ei : aux_graph[C].content[Ti]) {
        for (const Event &ej : aux_graph[B].content[Tj]) {
          if (ej.conflict(ei)) {
            aux_graph[A].edges.insert(std::make_pair(Tj, Ti));
            return;
          }
        }
      }
    }

    for (const Event &ei : aux_graph[C].content[Ti]) {
      for (const auto &[_, Tk] : aux_graph[B].last_transaction) {
        if (Ti != parent.at(Tk) &&
            contains_event(
                aux_graph[B].content[Tk],
                Event{"", EventType::begin, "", Annotation::undefined})) {
          for (const Event &ek : aux_graph[B].content[Tk]) {
            if (ek.conflict(ei) &&
                (Tk.thread == Tj.thread ||
                 aux_graph[B].reachable(child.at(std::make_pair(B, Tj)), Tk,
                                        aux_graph[B].reversed_vertices))) {
              aux_graph[A].edges.insert(std::make_pair(Tj, Ti));
              return;
            }
          }
        }
      }
    }

    for (const Event &ei : BHRS[std::make_pair(A, Ti)]) {
      for (const Event &ej : aux_graph[B].content[Tj]) {
        if (ej.conflict(ei)) {
          aux_graph[A].edges.insert(std::make_pair(Tj, Ti));
          return;
        }
      }
    }

    for (const Event &ei : BHRS[std::make_pair(A, Ti)]) {
      for (const auto &[_, Tk] : aux_graph[B].last_transaction) {
        if (contains_event(
                aux_graph[B].content[Tk],
                Event{"", EventType::begin, "", Annotation::undefined})) {
          for (const Event &ek : aux_graph[B].content[Tk]) {
            if (ek.conflict(ei) &&
                (Tk.thread == Tj.thread ||
                 aux_graph[B].reachable(child.at(std::make_pair(B, Tj)), Tk,
                                        aux_graph[B].reversed_vertices))) {
              aux_graph[A].edges.insert(std::make_pair(Tj, Ti));
              return;
            }
          }
        }
      }
    }

    for (const Event &ei : BHRS[std::make_pair(A, Ti)]) {
      Transaction Tk = aux_graph[C].first_transaction[std::make_tuple(
          ei.thread, EventType::undefined, "")];
      if (contains_event(
              aux_graph[A].content[parent.at(Tk)],
              Event{"", EventType::begin, "", Annotation::undefined})) {
        if (Tk.thread == Tj.thread ||
            aux_graph[C].edges.contains(
                std::make_pair(child.at(std::make_pair(C, Tj)), Tk))) {
          aux_graph[A].edges.insert(std::make_pair(Tj, Ti));
          return;
        }
      }
    }
  }

  void compute_reversed_edges(
      Nonterminal A, Nonterminal B, Nonterminal C,
      const std::unordered_map<Transaction, Transaction> &parent,
      const std::unordered_map<std::pair<Nonterminal, Transaction>, Transaction>
          &child) {

    for (const Transaction &Ti : aux_graph[A].reversed_vertices) {
      for (const Transaction &Tj : aux_graph[A].reversed_vertices) {
        compute_reversed_edge(Tj, Ti, A, B, C, parent, child);
      }
    }

    for (const auto &[_, Ti] : aux_graph[A].last_transaction) {
      if (aux_graph[A].reversed_vertices.contains(Ti)) {
        continue;
      }
      for (const Transaction &Tj : aux_graph[A].reversed_vertices) {
        compute_reversed_edge(Tj, Ti, A, B, C, parent, child);
      }
    }
  }

  void compute_aux_graph_two(Nonterminal A, Nonterminal B, Nonterminal C) {

#ifdef DEBUG
    std::cout << "Engine: compute_aux_graph (" << A << " -> " << B << " " << C
              << ")\n";
#endif

    std::unordered_map<Transaction, Transaction> parent;
    std::unordered_map<std::pair<Nonterminal, Transaction>, Transaction> child;

#ifdef DEBUG
    std::cout << "Before merge_routine\n";
    std::cout << aux_graph[A] << "\n";
#endif

    merge_routine(A, B, C, parent, child);

#ifdef DEBUG
    std::cout << "After merge_routine\n";
    std::cout << aux_graph[A] << "\n";
#endif

    compute_vertices(A, B, C, parent, child);

#ifdef DEBUG
    std::cout << "After compute_vertices\n";
    std::cout << aux_graph[A] << "\n";
#endif

    compute_reversed_vertices(A, B, C, parent, child);

#ifdef DEBUG
    std::cout << "After compute_reversed_vertices\n";
    std::cout << aux_graph[A] << "\n";
#endif

    compute_first_transactions(A, B, C, parent, child);

#ifdef DEBUG
    std::cout << "After compute_first_transactions\n";
    std::cout << aux_graph[A] << "\n";
#endif

    compute_last_transactions(A, B, C, parent, child);

#ifdef DEBUG
    std::cout << "After compute_last_transactions\n";
    std::cout << aux_graph[A] << "\n";
#endif

    compute_summaries(A, B, C, parent);

#ifdef DEBUG
    std::cout << "After compute_summaries\n";
    std::cout << aux_graph[A] << "\n";
#endif

    compute_reversed_summaries(A, B, C, parent);

#ifdef DEBUG
    std::cout << "After compute_reversed_summaries\n";
    std::cout << aux_graph[A] << "\n";
#endif

    compute_edges(A, B, C, parent, child);

#ifdef DEBUG
    std::cout << "After compute_edges\n";
    std::cout << aux_graph[A] << "\n";
#endif

    compute_reversed_edges(A, B, C, parent, child);

#ifdef DEBUG
    std::cout << "After compute_reversed_edges\n";
    std::cout << aux_graph[A] << "\n";
#endif
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
      if (vertex.idx == 0) {
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
      if (reversed_vertex.idx == 0) {
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

#ifdef DEBUG
    std::cout << "Engine: analyze_csv(" + nonterminal + ")\n";
    grammar.generate(nonterminal);
    std::cout << "\n";
#endif

    std::vector<Symbol> chunks = grammar.rules[nonterminal];

    if (chunks.size() == 1) {
      csv[nonterminal] = false;
      compute_aux_graph(nonterminal);
      return csv[nonterminal];

    } else if (chunks.size() == 2) {
      compute_cross_graph(nonterminal);

#ifdef DEBUG
      std::cout << "Cross graph:\n";
      std::cout << cross_graph[nonterminal] << "\n";
#endif

      bool violation = cross_graph[nonterminal].cyclic();
      csv[nonterminal] = violation;
      if (!violation) {
        compute_aux_graph(nonterminal);
      }
      return violation;
    }

    std::cerr << "Engine: Grammar is not in CNF\n";
    std::exit(EXIT_FAILURE);
  }

  void topological_sort_dfs(Nonterminal &vertex,
                            std::unordered_set<Nonterminal> &visited,
                            std::vector<Nonterminal> &result) {
    visited.insert(vertex);
    for (Symbol neighbor : grammar.rules.at(vertex)) {
      if (grammar.nonterminals.contains(neighbor)) {
        if (!visited.contains(neighbor)) {
          topological_sort_dfs(neighbor, visited, result);
        }
      }
    }
    result.push_back(vertex);
  }

  std::vector<Nonterminal> topological_sort() {
    Nonterminal start{"0"};
    std::unordered_set<Nonterminal> visited;
    std::vector<Nonterminal> result;
    topological_sort_dfs(start, visited, result);
    return result;
  }

  void analyze(std::string map_path, std::string grammar_path) {
    std::tie(grammar, threads, variables, locks) =
        Parser{}.parse(map_path, grammar_path);
    topological_ordering = topological_sort();
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
