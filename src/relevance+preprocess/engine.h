#ifndef ENGINE_H
#define ENGINE_H

#include "event.h"
#include "grammar.h"
#include "graph.h"
#include "parser.h"
#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <tuple>
#include <unordered_map>
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

  // Core of the engine
  struct Core {
    Grammar grammar;
    std::vector<Nonterminal> topological_ordering;

    std::unordered_map<Nonterminal, Graph> aux_graph;
    std::unordered_map<Nonterminal, Graph> cross_graph;
    std::unordered_map<Nonterminal, bool> csv;

    std::unordered_set<Thread> threads;
    std::unordered_set<Operand> variables;
    std::unordered_set<Operand> locks;

    // These are required for edges computation
    std::unordered_map<std::pair<Nonterminal, Transaction>,
                       std::unordered_set<Event>>
        THS; // top half summary
    std::unordered_map<std::pair<Nonterminal, Transaction>,
                       std::unordered_set<Event>>
        BHRS; // bottom half reversed summary

    TransactionIdx fresh_transaction_idx;

    Core()
        : grammar{}, topological_ordering{}, aux_graph{}, cross_graph{}, csv{},
          threads{}, variables{}, locks{}, THS{}, BHRS{}, fresh_transaction_idx{
                                                              1} {}
  };

  // For memorization
  struct Memo {
    std::unordered_map<std::pair<Nonterminal, Transaction>,
                       std::unordered_set<Event>>
        TUS; // trickle up summary
    std::unordered_map<std::pair<Nonterminal, Transaction>,
                       std::unordered_set<Event>>
        TURS; // trickle up reversed summary

    Memo() : TUS{}, TURS{} {}
  };

  // For relevant variables optimization
  struct Relevance {
    std::unordered_map<Nonterminal, std::unordered_set<Operand>> operands;
    std::unordered_map<Operand, std::unordered_set<Nonterminal>>
        lowest_occurences;
    std::unordered_map<Nonterminal, std::unordered_set<Nonterminal>> parents;
    std::unordered_map<Nonterminal, std::unordered_set<Nonterminal>> ancestors;

    std::unordered_map<Operand, Nonterminal> lowest_common_ancestor;
    std::unordered_map<Nonterminal, std::unordered_set<Operand>> maintain;

    Relevance()
        : operands{}, lowest_occurences{}, parents{}, ancestors{},
          lowest_common_ancestor{}, maintain{} {}
  };

  Core core;
  Memo memo;
  Relevance relv;

  Engine() : core{}, memo{}, relv{} {}

  Transaction make_transaction() {
    return Transaction{core.fresh_transaction_idx++};
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
                     std::unordered_map<std::pair<Nonterminal, Transaction>,
                                        Transaction> &parent,
                     std::unordered_map<std::pair<Nonterminal, Transaction>,
                                        Transaction> &child) {

    std::unordered_map<Thread, Transaction> cross_transaction;

    for (const Transaction &vertex : core.aux_graph[B].vertices) {
      if (vertex.idx == 0) {
        continue;
      }

      Transaction txn = make_transaction();
      txn.thread = vertex.thread;

      parent[std::make_pair(B, vertex)] = txn;
      child[std::make_pair(B, txn)] = vertex;

      core.aux_graph[A].content[txn] = core.aux_graph[B].content[vertex];

      cross_transaction[txn.thread] = txn;
    }

    for (const Transaction &reversed_vertex :
         core.aux_graph[C].reversed_vertices) {

      if (cross_transaction.contains(reversed_vertex.thread)) {

        if (cross_transaction[reversed_vertex.thread].thread !=
            reversed_vertex.thread) {
          std::cerr << "merge_routine: something went wrong (1)\n";
          std::exit(EXIT_FAILURE);
        }

        Transaction txn = cross_transaction[reversed_vertex.thread];

        parent[std::make_pair(C, reversed_vertex)] = txn;
        child[std::make_pair(C, txn)] = reversed_vertex;

        Annotation b_annotation = core.aux_graph[B]
                                      .content[child[std::make_pair(B, txn)]]
                                      .begin()
                                      ->annotation;
        Annotation c_annotation = core.aux_graph[C]
                                      .content[child[std::make_pair(C, txn)]]
                                      .begin()
                                      ->annotation;

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
          std::cerr << "merge_routine: something went wrong (2)\n";
          std::exit(EXIT_FAILURE);
        }

        core.aux_graph[A].content[txn].clear();

        for (const Event &event :
             core.aux_graph[B].content[child[std::make_pair(B, txn)]]) {
          core.aux_graph[A].content[txn].insert(
              Event{event.thread, event.type, event.operand, a_annotation});
        }

        for (const Event &event :
             core.aux_graph[C].content[child[std::make_pair(C, txn)]]) {
          core.aux_graph[A].content[txn].insert(
              Event{event.thread, event.type, event.operand, a_annotation});
        }

      } else {

        Transaction txn = make_transaction();
        txn.thread = reversed_vertex.thread;

        parent[std::make_pair(C, reversed_vertex)] = txn;
        child[std::make_pair(C, txn)] = reversed_vertex;

        core.aux_graph[A].content[txn] =
            core.aux_graph[C].content[reversed_vertex];

        cross_transaction[txn.thread] = txn;
      }
    }
  }

  void compute_vertices(Nonterminal A, Nonterminal B, Nonterminal C,
                        std::unordered_map<std::pair<Nonterminal, Transaction>,
                                           Transaction> &parent,
                        std::unordered_map<std::pair<Nonterminal, Transaction>,
                                           Transaction> &child) {

    for (const Transaction &vertex : core.aux_graph[C].vertices) {

      if (parent.contains(std::make_pair(C, vertex))) {
        core.aux_graph[A].vertices.insert(parent[std::make_pair(C, vertex)]);

      } else {

        Transaction txn = make_transaction();
        txn.thread = vertex.thread;

        parent[std::make_pair(C, vertex)] = txn;
        child[std::make_pair(C, txn)] = vertex;

        core.aux_graph[A].content[txn] = core.aux_graph[C].content[vertex];

        core.aux_graph[A].vertices.insert(txn);
      }
    }

    for (const Transaction &vertex : core.aux_graph[B].vertices) {

      if (!parent.contains(std::make_pair(B, vertex))) {
        std::cerr << "compute_vertices: something went wrong\n";
        std::exit(EXIT_FAILURE);
      }

      if (!contains_event(
              core.aux_graph[A].content[parent[std::make_pair(B, vertex)]],
              Event{"", EventType::end, "", Annotation::undefined})) {

        core.aux_graph[A].vertices.insert(parent[std::make_pair(B, vertex)]);
      }
    }
  }

  void compute_reversed_vertices(
      Nonterminal A, Nonterminal B, Nonterminal C,
      std::unordered_map<std::pair<Nonterminal, Transaction>, Transaction>
          &parent,
      std::unordered_map<std::pair<Nonterminal, Transaction>, Transaction>
          &child) {

    for (const Transaction &reversed_vertex :
         core.aux_graph[B].reversed_vertices) {

      if (parent.contains(std::make_pair(B, reversed_vertex))) {
        core.aux_graph[A].reversed_vertices.insert(
            parent[std::make_pair(B, reversed_vertex)]);

      } else {

        Transaction txn = make_transaction();
        txn.thread = reversed_vertex.thread;

        parent[std::make_pair(B, reversed_vertex)] = txn;
        child[std::make_pair(B, txn)] = reversed_vertex;

        core.aux_graph[A].content[txn] =
            core.aux_graph[B].content[reversed_vertex];

        core.aux_graph[A].reversed_vertices.insert(txn);
      }
    }

    for (const Transaction &reversed_vertex :
         core.aux_graph[C].reversed_vertices) {

      if (!parent.contains(std::make_pair(C, reversed_vertex))) {
        std::cerr << "compute_reversed_vertices: something went wrong\n";
        std::exit(EXIT_FAILURE);
      }

      if (!contains_event(
              core.aux_graph[A]
                  .content[parent[std::make_pair(C, reversed_vertex)]],
              Event{"", EventType::begin, "", Annotation::undefined})) {

        core.aux_graph[A].reversed_vertices.insert(
            parent[std::make_pair(C, reversed_vertex)]);
      }
    }
  }

  void compute_first_transactions(
      Nonterminal A, Nonterminal B, Nonterminal C,
      std::unordered_map<std::pair<Nonterminal, Transaction>, Transaction>
          &parent,
      std::unordered_map<std::pair<Nonterminal, Transaction>, Transaction>
          &child) {

    for (const auto &[decor, ftxn] : core.aux_graph[B].first_transaction) {

      EventType type = std::get<1>(decor);
      if (type == EventType::read || type == EventType::write ||
          type == EventType::lock || type == EventType::unlock) {
        Operand operand = std::get<2>(decor);
        if (!relv.maintain[A].contains(operand)) {
          continue;
#ifdef DEBUG
          std::cout << "reduction!\n";
#endif
        }
      }

      if (parent.contains(std::make_pair(B, ftxn))) {
        core.aux_graph[A].first_transaction[decor] =
            parent[std::make_pair(B, ftxn)];

      } else {

        Transaction txn = make_transaction();
        txn.thread = ftxn.thread;

        parent[std::make_pair(B, ftxn)] = txn;
        child[std::make_pair(B, txn)] = ftxn;

        core.aux_graph[A].content[txn] = core.aux_graph[B].content[ftxn];

        core.aux_graph[A].first_transaction[decor] = txn;
      }
    }

    for (const auto &[decor, ftxn] : core.aux_graph[C].first_transaction) {

      if (core.aux_graph[A].first_transaction.contains(decor)) {
        continue;
      }

      EventType type = std::get<1>(decor);
      if (type == EventType::read || type == EventType::write ||
          type == EventType::lock || type == EventType::unlock) {
        Operand operand = std::get<2>(decor);
        if (!relv.maintain[A].contains(operand)) {
          continue;
#ifdef DEBUG
          std::cout << "reduction!\n";
#endif
        }
      }

      if (parent.contains(std::make_pair(C, ftxn))) {
        core.aux_graph[A].first_transaction[decor] =
            parent[std::make_pair(C, ftxn)];

      } else {

        Transaction txn = make_transaction();
        txn.thread = ftxn.thread;

        parent[std::make_pair(C, ftxn)] = txn;
        child[std::make_pair(C, txn)] = ftxn;

        core.aux_graph[A].content[txn] = core.aux_graph[C].content[ftxn];

        core.aux_graph[A].first_transaction[decor] = txn;
      }
    }
  }

  void compute_last_transactions(
      Nonterminal A, Nonterminal B, Nonterminal C,
      std::unordered_map<std::pair<Nonterminal, Transaction>, Transaction>
          &parent,
      std::unordered_map<std::pair<Nonterminal, Transaction>, Transaction>
          &child) {

    for (const auto &[decor, ltxn] : core.aux_graph[C].last_transaction) {

      EventType type = std::get<1>(decor);
      if (type == EventType::read || type == EventType::write ||
          type == EventType::lock || type == EventType::unlock) {
        Operand operand = std::get<2>(decor);
        if (!relv.maintain[A].contains(operand)) {
          continue;
#ifdef DEBUG
          std::cout << "reduction!\n";
#endif
        }
      }

      if (parent.contains(std::make_pair(C, ltxn))) {
        core.aux_graph[A].last_transaction[decor] =
            parent[std::make_pair(C, ltxn)];

      } else {

        Transaction txn = make_transaction();
        txn.thread = ltxn.thread;

        parent[std::make_pair(C, ltxn)] = txn;
        child[std::make_pair(C, txn)] = ltxn;

        core.aux_graph[A].content[txn] = core.aux_graph[C].content[ltxn];

        core.aux_graph[A].last_transaction[decor] = txn;
      }
    }

    for (const auto &[decor, ltxn] : core.aux_graph[B].last_transaction) {

      if (core.aux_graph[A].last_transaction.contains(decor)) {
        continue;
      }

      EventType type = std::get<1>(decor);
      if (type == EventType::read || type == EventType::write ||
          type == EventType::lock || type == EventType::unlock) {
        Operand operand = std::get<2>(decor);
        if (!relv.maintain[A].contains(operand)) {
          continue;
#ifdef DEBUG
          std::cout << "reduction!\n";
#endif
        }
      }

      if (parent.contains(std::make_pair(B, ltxn))) {
        core.aux_graph[A].last_transaction[decor] =
            parent[std::make_pair(B, ltxn)];

      } else {

        Transaction txn = make_transaction();
        txn.thread = ltxn.thread;

        parent[std::make_pair(B, ltxn)] = txn;
        child[std::make_pair(B, txn)] = ltxn;

        core.aux_graph[A].content[txn] = core.aux_graph[B].content[ltxn];

        core.aux_graph[A].last_transaction[decor] = txn;
      }
    }
  }

  std::unordered_set<Event> compute_neighbor_summary_fixpoint(
      Transaction Ti, Nonterminal A, Nonterminal B,
      const std::unordered_map<std::pair<Nonterminal, Transaction>, Transaction>
          &parent,
      std::unordered_set<Thread> &avoid) {

    if (avoid.contains(Ti.thread)) {
      return std::unordered_set<Event>{};
    }

    avoid.insert(Ti.thread);

    std::unordered_set<Event> res;

    for (const auto &edge : core.aux_graph[B].edges) {

      if (Ti != edge.first) {
        continue;
      }

      Transaction Tj = edge.second;

      if (!core.aux_graph[B].vertices.contains(Tj)) {
        continue;
      }

      if (!core.aux_graph[A].content.contains(
              parent.at(std::make_pair(B, Tj)))) {
        // Tj is not a cross-boundary transaction
        continue;
      }

      if (!contains_event(
              core.aux_graph[A].content[parent.at(std::make_pair(B, Tj))],
              Event{"", EventType::end, "", Annotation::undefined})) {
        // Tj is an active transaction
        continue;
      }

      for (const Event &event : core.aux_graph[B].content[Tj]) {

        Annotation annotation = Annotation::undefined;

        if (event.annotation == Annotation::open ||
            event.annotation == Annotation::up) {
          annotation = Annotation::up;
        } else if (event.annotation == Annotation::down ||
                   event.annotation == Annotation::closed) {
          annotation = Annotation::closed;
        } else {
          std::cerr
              << "compute_neighbor_summary_fixpoint: something went wrong\n";
          std::exit(EXIT_FAILURE);
        }

        res.insert(Event{event.thread, event.type, event.operand, annotation});
      }

      res.insert(core.aux_graph[B].summary[Tj].begin(),
                 core.aux_graph[B].summary[Tj].end());

      std::unordered_set<Event> neighbor_summary =
          compute_neighbor_summary_fixpoint(Tj, A, B, parent, avoid);

      res.insert(neighbor_summary.begin(), neighbor_summary.end());
    }

    return res;
  }

  std::unordered_set<Event> compute_neighbor_summary(
      Transaction Ti, Nonterminal A, Nonterminal B,
      const std::unordered_map<std::pair<Nonterminal, Transaction>, Transaction>
          &parent) {

    std::unordered_set<Thread> avoid;

    return compute_neighbor_summary_fixpoint(Ti, A, B, parent, avoid);
  }

  std::unordered_set<Event> compute_trickle_down_summary(
      Transaction Ti, Nonterminal B, Nonterminal C,
      const std::unordered_set<Event> &neighbor_summary,
      const std::unordered_map<std::pair<Nonterminal, Transaction>, Transaction>
          &parent,
      Nonterminal A) {

    std::unordered_set<Event> top_events;

    top_events.insert(core.aux_graph[B].content[Ti].begin(),
                      core.aux_graph[B].content[Ti].end());
    top_events.insert(core.aux_graph[B].summary[Ti].begin(),
                      core.aux_graph[B].summary[Ti].end());
    top_events.insert(neighbor_summary.begin(), neighbor_summary.end());

    std::vector<std::pair<Event, Event>> args;

    for (const Thread &ui : core.threads) {
      args.push_back(std::make_pair(
          Event{ui, EventType::undefined, "", Annotation::undefined},
          Event{ui, EventType::undefined, "", Annotation::undefined}));
      args.push_back(std::make_pair(
          Event{"", EventType::fork, ui, Annotation::undefined},
          Event{ui, EventType::undefined, "", Annotation::undefined}));
      for (const Operand &op : relv.maintain[A]) {
        if (core.variables.contains(op)) {
          args.push_back(std::make_pair(
              Event{"", EventType::write, op, Annotation::undefined},
              Event{ui, EventType::read, op, Annotation::undefined}));
          args.push_back(std::make_pair(
              Event{"", EventType::write, op, Annotation::undefined},
              Event{ui, EventType::write, op, Annotation::undefined}));
          args.push_back(std::make_pair(
              Event{"", EventType::read, op, Annotation::undefined},
              Event{ui, EventType::write, op, Annotation::undefined}));
        } else if (core.locks.contains(op)) {
          args.push_back(std::make_pair(
              Event{"", EventType::unlock, op, Annotation::undefined},
              Event{ui, EventType::lock, op, Annotation::undefined}));
        } else {
          std::cerr << "compute_trickle_down_summary: something went wrong\n";
          std::exit(EXIT_FAILURE);
        }
      }
      for (const Thread &uj : core.threads) {
        args.push_back(std::make_pair(
            Event{uj, EventType::undefined, "", Annotation::undefined},
            Event{ui, EventType::join, uj, Annotation::undefined}));
      }
    }

    std::unordered_set<Event> res;

    for (const std::pair<Event, Event> &arg : args) {

      if (!contains_event(top_events, arg.first)) {
        continue;
      }

      std::tuple<Thread, EventType, Operand> decor{
          arg.second.thread, arg.second.type, arg.second.operand};

      if (!core.aux_graph[C].first_transaction.contains(decor)) {
        continue;
      }

      Transaction Tj = core.aux_graph[C].first_transaction[decor];

      if (parent.contains(std::make_pair(B, Ti)) &&
          parent.contains(std::make_pair(C, Tj)) &&
          parent.at(std::make_pair(B, Ti)) ==
              parent.at(std::make_pair(C, Tj))) {
        // Ti and Tj are the same transaction
        continue;
      }

      if (!contains_event(
              core.aux_graph[C].content[Tj],
              Event{"", EventType::end, "", Annotation::undefined})) {
        // Tj is an active transaction
        continue;
      }

      res.insert(core.aux_graph[C].content[Tj].begin(),
                 core.aux_graph[C].content[Tj].end());
      res.insert(core.aux_graph[C].summary[Tj].begin(),
                 core.aux_graph[C].summary[Tj].end());
    }

    return res;
  }

  std::unordered_set<Event>
  annotate_bottom_summary(Nonterminal B,
                          const std::unordered_set<Event> &summary) {

    std::unordered_set<Event> res;

    for (const Event &event : summary) {

      if (event.annotation == Annotation::closed ||
          event.annotation == Annotation::down) {

        res.insert(
            Event{event.thread, event.type, event.operand, event.annotation});

      } else if (event.annotation == Annotation::up ||
                 event.annotation == Annotation::open) {

        std::tuple<Thread, EventType, Operand> decor{event.thread,
                                                     EventType::undefined, ""};

        if (!core.aux_graph[B].last_transaction.contains(decor)) {
          res.insert(
              Event{event.thread, event.type, event.operand, event.annotation});
          continue;
        }

        Transaction Tj = core.aux_graph[B].last_transaction[decor];

        Annotation annotation = event.annotation;

        if (contains_event(
                core.aux_graph[B].content[Tj],
                Event{"", EventType::begin, "", Annotation::undefined})) {

          if (annotation == Annotation::up) {
            annotation = Annotation::closed;
          } else if (annotation == Annotation::open) {
            annotation = Annotation::down;
          }
        }

        res.insert(Event{event.thread, event.type, event.operand, annotation});

      } else {
        std::cerr << "annotate_bottom_summary: something went wrong\n";
        std::exit(EXIT_FAILURE);
      }
    }

    return res;
  }

  std::unordered_set<Event> compute_trickle_up_summary_fixpoint(
      Transaction Ti, Transaction Tj, Nonterminal A, Nonterminal B,
      Nonterminal C,
      const std::unordered_map<std::pair<Nonterminal, Transaction>, Transaction>
          &parent,
      const std::unordered_map<std::pair<Nonterminal, Transaction>, Transaction>
          &child,
      std::unordered_set<Thread> &avoid) {

    if (avoid.contains(Tj.thread)) {
      return std::unordered_set<Event>{};
    }

    avoid.insert(Tj.thread);

    std::unordered_set<Event> neighbor_summary =
        compute_neighbor_summary(Tj, A, B, parent);

    std::unordered_set<Event> trickle_down_summary =
        compute_trickle_down_summary(Tj, B, C, neighbor_summary, parent, A);

    std::unordered_set<Event> annotated_trickle_down_summary =
        annotate_bottom_summary(B, trickle_down_summary);

    std::unordered_set<Event> trickle_up_summary;

    for (const Event &event : trickle_down_summary) {
      if (avoid.contains(event.thread)) {
        continue;
      }

      if (event.annotation == Annotation::up ||
          event.annotation == Annotation::open) {

        std::tuple<Thread, EventType, Operand> decor{event.thread,
                                                     EventType::undefined, ""};

        if (!core.aux_graph[B].last_transaction.contains(decor)) {
          continue;
        }

        Transaction Tk = core.aux_graph[B].last_transaction[decor];

        std::unordered_set<Event> res = compute_trickle_up_summary_fixpoint(
            Ti, Tk, A, B, C, parent, child, avoid);

        trickle_up_summary.insert(res.begin(), res.end());
      }
    }

    std::unordered_set<Event> res;

    res.insert(core.aux_graph[B].content[Tj].begin(),
               core.aux_graph[B].content[Tj].end());
    res.insert(core.aux_graph[B].summary[Tj].begin(),
               core.aux_graph[B].summary[Tj].end());
    res.insert(neighbor_summary.begin(), neighbor_summary.end());
    res.insert(annotated_trickle_down_summary.begin(),
               annotated_trickle_down_summary.end());
    res.insert(trickle_up_summary.begin(), trickle_up_summary.end());

    core.THS[std::make_pair(A, Ti)].insert(
        core.aux_graph[B].content[Tj].begin(),
        core.aux_graph[B].content[Tj].end());
    core.THS[std::make_pair(A, Ti)].insert(
        core.aux_graph[B].summary[Tj].begin(),
        core.aux_graph[B].summary[Tj].end());
    core.THS[std::make_pair(A, Ti)].insert(neighbor_summary.begin(),
                                           neighbor_summary.end());

    return res;
  }

  std::unordered_set<Event> compute_trickle_up_summary(
      Transaction Ti, Transaction Tj, Nonterminal A, Nonterminal B,
      Nonterminal C,
      const std::unordered_map<std::pair<Nonterminal, Transaction>, Transaction>
          &parent,
      const std::unordered_map<std::pair<Nonterminal, Transaction>, Transaction>
          &child) {

    std::pair<Nonterminal, Transaction> decor{A, Ti};

    if (memo.TUS.contains(decor)) {
      return memo.TUS[decor];
    }

    std::unordered_set<Thread> avoid;

    memo.TUS[decor] = compute_trickle_up_summary_fixpoint(Ti, Tj, A, B, C,
                                                          parent, child, avoid);

    return memo.TUS[decor];
  }

  void compute_summary(
      Transaction Ti, Nonterminal A, Nonterminal B, Nonterminal C,
      const std::unordered_map<std::pair<Nonterminal, Transaction>, Transaction>
          &parent,
      const std::unordered_map<std::pair<Nonterminal, Transaction>, Transaction>
          &child) {

    if (core.aux_graph[A].summary.contains(Ti)) {
      return;
    }

#ifdef DEBUG
    std::cout << "Engine: compute_summary(Ti = " << Ti << ", A = " << A
              << ", B = " << B << ", C = " << C << ")\n";
#endif

    std::unordered_set<Event> b_summary;

    if (child.contains(std::make_pair(B, Ti))) {
      b_summary = core.aux_graph[B].summary[child.at(std::make_pair(B, Ti))];
    }

    std::unordered_set<Event> neighbor_summary;

    if (child.contains(std::make_pair(B, Ti))) {
      neighbor_summary = compute_neighbor_summary(
          child.at(std::make_pair(B, Ti)), A, B, parent);
    }

#ifdef DEBUG
    std::cout << "NS =";
    for (const Event &event : neighbor_summary) {
      std::cout << " " << event;
    }
    std::cout << "\n";
#endif

    std::unordered_set<Event> trickle_down_summary;

    if (child.contains(std::make_pair(B, Ti))) {
      trickle_down_summary = compute_trickle_down_summary(
          child.at(std::make_pair(B, Ti)), B, C, neighbor_summary, parent, A);
    }

#ifdef DEBUG
    std::cout << "TDS =";
    for (const Event &event : trickle_down_summary) {
      std::cout << " " << event;
    }
    std::cout << "\n";
#endif

    std::unordered_set<Event> c_summary;

    if (child.contains(std::make_pair(C, Ti))) {
      c_summary = core.aux_graph[C].summary[child.at(std::make_pair(C, Ti))];
    }

    std::unordered_set<Event> bottom_summary;

    bottom_summary.insert(trickle_down_summary.begin(),
                          trickle_down_summary.end());
    bottom_summary.insert(c_summary.begin(), c_summary.end());

    std::unordered_set<Event> annotated_bottom_summary =
        annotate_bottom_summary(B, bottom_summary);

    std::unordered_set<Event> trickle_up_summary;

    for (const Event &event : bottom_summary) {
      if (event.thread == Ti.thread) {
        continue;
      }

      if (event.annotation == Annotation::up ||
          event.annotation == Annotation::open) {

        std::tuple<Thread, EventType, Operand> decor{event.thread,
                                                     EventType::undefined, ""};

        if (!core.aux_graph[B].last_transaction.contains(decor)) {
          continue;
        }

        Transaction Tj = core.aux_graph[B].last_transaction[decor];

        std::unordered_set<Event> res =
            compute_trickle_up_summary(Ti, Tj, A, B, C, parent, child);

        trickle_up_summary.insert(res.begin(), res.end());
      }
    }

    core.aux_graph[A].summary[Ti].insert(b_summary.begin(), b_summary.end());
    core.aux_graph[A].summary[Ti].insert(neighbor_summary.begin(),
                                         neighbor_summary.end());
    core.aux_graph[A].summary[Ti].insert(annotated_bottom_summary.begin(),
                                         annotated_bottom_summary.end());
    core.aux_graph[A].summary[Ti].insert(trickle_up_summary.begin(),
                                         trickle_up_summary.end());

    core.THS[std::make_pair(A, Ti)].insert(b_summary.begin(), b_summary.end());
    core.THS[std::make_pair(A, Ti)].insert(neighbor_summary.begin(),
                                           neighbor_summary.end());
  }

  void compute_summaries(
      Nonterminal A, Nonterminal B, Nonterminal C,
      const std::unordered_map<std::pair<Nonterminal, Transaction>, Transaction>
          &parent,
      const std::unordered_map<std::pair<Nonterminal, Transaction>, Transaction>
          &child) {

    for (const Transaction &Ti : core.aux_graph[A].vertices) {
      compute_summary(Ti, A, B, C, parent, child);
    }

    for (const auto &[_, Ti] : core.aux_graph[A].first_transaction) {
      compute_summary(Ti, A, B, C, parent, child);
    }
  }

  std::unordered_set<Event> compute_neighbor_reversed_summary_fixpoint(
      Transaction Ti, Nonterminal A, Nonterminal C,
      const std::unordered_map<std::pair<Nonterminal, Transaction>, Transaction>
          &parent,
      std::unordered_set<Thread> &avoid) {

    if (avoid.contains(Ti.thread)) {
      return std::unordered_set<Event>{};
    }

    avoid.insert(Ti.thread);

    std::unordered_set<Event> res;

    for (const auto &edge : core.aux_graph[C].edges) {

      if (Ti != edge.second) {
        continue;
      }

      Transaction Tj = edge.first;

      if (!core.aux_graph[C].reversed_vertices.contains(Tj)) {
        continue;
      }

      if (!core.aux_graph[A].content.contains(
              parent.at(std::make_pair(C, Tj)))) {
        // Tj is not a cross-boundary transaction
        continue;
      }

      if (!contains_event(
              core.aux_graph[A].content[parent.at(std::make_pair(C, Tj))],
              Event{"", EventType::begin, "", Annotation::undefined})) {
        // Tj is a reversed active transaction
        continue;
      }

      for (const Event &event : core.aux_graph[C].content[Tj]) {

        Annotation annotation = Annotation::undefined;

        if (event.annotation == Annotation::open ||
            event.annotation == Annotation::down) {
          annotation = Annotation::down;
        } else if (event.annotation == Annotation::up ||
                   event.annotation == Annotation::closed) {
          annotation = Annotation::closed;
        } else {
          std::cerr << "compute_neighbor_reversed_summary_fixpoint: "
                       "something went wrong\n";
          std::exit(EXIT_FAILURE);
        }

        res.insert(Event{event.thread, event.type, event.operand, annotation});
      }

      res.insert(core.aux_graph[C].reversed_summary[Tj].begin(),
                 core.aux_graph[C].reversed_summary[Tj].end());

      std::unordered_set<Event> neighbor_reversed_summary =
          compute_neighbor_reversed_summary_fixpoint(Tj, A, C, parent, avoid);

      res.insert(neighbor_reversed_summary.begin(),
                 neighbor_reversed_summary.end());
    }

    return res;
  }

  std::unordered_set<Event> compute_neighbor_reversed_summary(
      Transaction Ti, Nonterminal A, Nonterminal C,
      const std::unordered_map<std::pair<Nonterminal, Transaction>, Transaction>
          &parent) {

    std::unordered_set<Thread> avoid;

    return compute_neighbor_reversed_summary_fixpoint(Ti, A, C, parent, avoid);
  }

  std::unordered_set<Event> compute_trickle_down_reversed_summary(
      Transaction Ti, Nonterminal B, Nonterminal C,
      const std::unordered_set<Event> &neighbor_reversed_summary,
      const std::unordered_map<std::pair<Nonterminal, Transaction>, Transaction>
          &parent,
      Nonterminal A) {

    std::unordered_set<Event> bottom_events;

    bottom_events.insert(core.aux_graph[C].content[Ti].begin(),
                         core.aux_graph[C].content[Ti].end());
    bottom_events.insert(core.aux_graph[C].summary[Ti].begin(),
                         core.aux_graph[C].summary[Ti].end());
    bottom_events.insert(neighbor_reversed_summary.begin(),
                         neighbor_reversed_summary.end());

    std::vector<std::pair<Event, Event>> args;

    for (const Thread &ui : core.threads) {
      args.push_back(std::make_pair(
          Event{ui, EventType::undefined, "", Annotation::undefined},
          Event{ui, EventType::undefined, "", Annotation::undefined}));
      args.push_back(std::make_pair(
          Event{ui, EventType::undefined, "", Annotation::undefined},
          Event{"", EventType::join, ui, Annotation::undefined}));
      for (const Operand &op : relv.maintain[A]) {
        if (core.variables.contains(op)) {
          args.push_back(std::make_pair(
              Event{ui, EventType::read, op, Annotation::undefined},
              Event{"", EventType::write, op, Annotation::undefined}));
          args.push_back(std::make_pair(
              Event{ui, EventType::write, op, Annotation::undefined},
              Event{"", EventType::write, op, Annotation::undefined}));
          args.push_back(std::make_pair(
              Event{ui, EventType::write, op, Annotation::undefined},
              Event{"", EventType::read, op, Annotation::undefined}));
        } else if (core.locks.contains(op)) {
          args.push_back(std::make_pair(
              Event{ui, EventType::unlock, op, Annotation::undefined},
              Event{"", EventType::lock, op, Annotation::undefined}));
        } else {
          std::cerr << "compute_trickle_down_reversed_summary: something went "
                       "wrong\n";
          std::exit(EXIT_FAILURE);
        }
      }
      for (const Thread &uj : core.threads) {
        args.push_back(std::make_pair(
            Event{ui, EventType::fork, uj, Annotation::undefined},
            Event{uj, EventType::undefined, "", Annotation::undefined}));
      }
    }

    std::unordered_set<Event> res;

    for (const std::pair<Event, Event> &arg : args) {

      if (!contains_event(bottom_events, arg.second)) {
        continue;
      }

      std::tuple<Thread, EventType, Operand> decor{
          arg.first.thread, arg.first.type, arg.first.operand};

      if (!core.aux_graph[B].last_transaction.contains(decor)) {
        continue;
      }

      Transaction Tj = core.aux_graph[B].last_transaction[decor];

      if (parent.contains(std::make_pair(C, Ti)) &&
          parent.contains(std::make_pair(B, Tj)) &&
          parent.at(std::make_pair(C, Ti)) ==
              parent.at(std::make_pair(B, Tj))) {
        // Ti and Tj are the same transaction
        continue;
      }

      if (!contains_event(
              core.aux_graph[B].content[Tj],
              Event{"", EventType::begin, "", Annotation::undefined})) {
        // Tj is a reversed active transaction
        continue;
      }

      res.insert(core.aux_graph[B].content[Tj].begin(),
                 core.aux_graph[B].content[Tj].end());
      res.insert(core.aux_graph[B].reversed_summary[Tj].begin(),
                 core.aux_graph[B].reversed_summary[Tj].end());
    }

    return res;
  }

  std::unordered_set<Event> annotate_top_reversed_summary(
      Nonterminal C, const std::unordered_set<Event> &reversed_summary) {

    std::unordered_set<Event> res;

    for (const Event &event : reversed_summary) {

      if (event.annotation == Annotation::closed ||
          event.annotation == Annotation::up) {

        res.insert(
            Event{event.thread, event.type, event.operand, event.annotation});

      } else if (event.annotation == Annotation::down ||
                 event.annotation == Annotation::open) {

        std::tuple<Thread, EventType, Operand> decor{event.thread,
                                                     EventType::undefined, ""};

        if (!core.aux_graph[C].first_transaction.contains(decor)) {
          res.insert(
              Event{event.thread, event.type, event.operand, event.annotation});
          continue;
        }

        Transaction Tj = core.aux_graph[C].first_transaction[decor];

        Annotation annotation = event.annotation;

        if (contains_event(
                core.aux_graph[C].content[Tj],
                Event{"", EventType::end, "", Annotation::undefined})) {

          if (annotation == Annotation::down) {
            annotation = Annotation::closed;
          } else if (annotation == Annotation::open) {
            annotation = Annotation::up;
          }
        }

        res.insert(Event{event.thread, event.type, event.operand, annotation});

      } else {
        std::cerr << "annotate_top_reversed_summary: something went wrong\n";
        std::exit(EXIT_FAILURE);
      }
    }

    return res;
  }

  std::unordered_set<Event> compute_trickle_up_reversed_summary_fixpoint(
      Transaction Ti, Transaction Tj, Nonterminal A, Nonterminal B,
      Nonterminal C,
      const std::unordered_map<std::pair<Nonterminal, Transaction>, Transaction>
          &parent,
      const std::unordered_map<std::pair<Nonterminal, Transaction>, Transaction>
          &child,
      std::unordered_set<Thread> &avoid) {

    if (avoid.contains(Tj.thread)) {
      return std::unordered_set<Event>{};
    }

    avoid.insert(Tj.thread);

    std::unordered_set<Event> neighbor_reversed_summary =
        compute_neighbor_reversed_summary(Tj, A, C, parent);

    std::unordered_set<Event> trickle_down_reversed_summary =
        compute_trickle_down_reversed_summary(
            Tj, B, C, neighbor_reversed_summary, parent, A);

    std::unordered_set<Event> annotated_trickle_down_reversed_summary =
        annotate_top_reversed_summary(C, trickle_down_reversed_summary);

    std::unordered_set<Event> trickle_up_reversed_summary;

    for (const Event &event : trickle_down_reversed_summary) {
      if (avoid.contains(event.thread)) {
        continue;
      }

      if (event.annotation == Annotation::down ||
          event.annotation == Annotation::open) {

        std::tuple<Thread, EventType, Operand> decor{event.thread,
                                                     EventType::undefined, ""};

        if (!core.aux_graph[C].first_transaction.contains(decor)) {
          continue;
        }

        Transaction Tk = core.aux_graph[C].first_transaction[decor];

        std::unordered_set<Event> res =
            compute_trickle_up_reversed_summary_fixpoint(Ti, Tk, A, B, C,
                                                         parent, child, avoid);

        trickle_up_reversed_summary.insert(res.begin(), res.end());
      }
    }

    std::unordered_set<Event> res;

    res.insert(core.aux_graph[C].content[Tj].begin(),
               core.aux_graph[C].content[Tj].end());
    res.insert(core.aux_graph[C].reversed_summary[Tj].begin(),
               core.aux_graph[C].reversed_summary[Tj].end());
    res.insert(neighbor_reversed_summary.begin(),
               neighbor_reversed_summary.end());
    res.insert(annotated_trickle_down_reversed_summary.begin(),
               annotated_trickle_down_reversed_summary.end());
    res.insert(trickle_up_reversed_summary.begin(),
               trickle_up_reversed_summary.end());

    core.BHRS[std::make_pair(A, Ti)].insert(
        core.aux_graph[C].content[Tj].begin(),
        core.aux_graph[C].content[Tj].end());
    core.BHRS[std::make_pair(A, Ti)].insert(
        core.aux_graph[C].reversed_summary[Tj].begin(),
        core.aux_graph[C].reversed_summary[Tj].end());
    core.BHRS[std::make_pair(A, Ti)].insert(neighbor_reversed_summary.begin(),
                                            neighbor_reversed_summary.end());

    return res;
  }

  std::unordered_set<Event> compute_trickle_up_reversed_summary(
      Transaction Ti, Transaction Tj, Nonterminal A, Nonterminal B,
      Nonterminal C,
      const std::unordered_map<std::pair<Nonterminal, Transaction>, Transaction>
          &parent,
      const std::unordered_map<std::pair<Nonterminal, Transaction>, Transaction>
          &child) {

    std::pair<Nonterminal, Transaction> decor{A, Ti};

    if (memo.TURS.contains(decor)) {
      return memo.TURS[decor];
    }

    std::unordered_set<Thread> avoid;

    memo.TURS[decor] = compute_trickle_up_reversed_summary_fixpoint(
        Ti, Tj, A, B, C, parent, child, avoid);

    return memo.TURS[decor];
  }

  void compute_reversed_summary(
      Transaction Ti, Nonterminal A, Nonterminal B, Nonterminal C,
      const std::unordered_map<std::pair<Nonterminal, Transaction>, Transaction>
          &parent,
      const std::unordered_map<std::pair<Nonterminal, Transaction>, Transaction>
          &child) {

    if (core.aux_graph[A].reversed_summary.contains(Ti)) {
      return;
    }

    std::unordered_set<Event> c_reversed_summary;

    if (child.contains(std::make_pair(C, Ti))) {
      c_reversed_summary =
          core.aux_graph[C].reversed_summary[child.at(std::make_pair(C, Ti))];
    }

    std::unordered_set<Event> neighbor_reversed_summary;

    if (child.contains(std::make_pair(C, Ti))) {
      neighbor_reversed_summary = compute_neighbor_reversed_summary(
          child.at(std::make_pair(C, Ti)), A, C, parent);
    }

    std::unordered_set<Event> trickle_down_reversed_summary;

    if (child.contains(std::make_pair(C, Ti))) {
      trickle_down_reversed_summary = compute_trickle_down_reversed_summary(
          child.at(std::make_pair(C, Ti)), B, C, neighbor_reversed_summary,
          parent, A);
    }

    std::unordered_set<Event> b_reversed_summary;

    if (child.contains(std::make_pair(B, Ti))) {
      b_reversed_summary =
          core.aux_graph[B].reversed_summary[child.at(std::make_pair(B, Ti))];
    }

    std::unordered_set<Event> top_reversed_summary;

    top_reversed_summary.insert(trickle_down_reversed_summary.begin(),
                                trickle_down_reversed_summary.end());
    top_reversed_summary.insert(b_reversed_summary.begin(),
                                b_reversed_summary.end());

    std::unordered_set<Event> annotated_top_reversed_summary =
        annotate_top_reversed_summary(C, top_reversed_summary);

    std::unordered_set<Event> trickle_up_reversed_summary;

    for (const Event &event : top_reversed_summary) {
      if (event.thread == Ti.thread) {
        continue;
      }

      if (event.annotation == Annotation::down ||
          event.annotation == Annotation::open) {

        std::tuple<Thread, EventType, Operand> decor{event.thread,
                                                     EventType::undefined, ""};

        if (!core.aux_graph[C].first_transaction.contains(decor)) {
          continue;
        }

        Transaction Tj = core.aux_graph[C].first_transaction[decor];

        std::unordered_set<Event> res =
            compute_trickle_up_reversed_summary(Ti, Tj, A, B, C, parent, child);

        trickle_up_reversed_summary.insert(res.begin(), res.end());
      }
    }

    core.aux_graph[A].reversed_summary[Ti].insert(c_reversed_summary.begin(),
                                                  c_reversed_summary.end());
    core.aux_graph[A].reversed_summary[Ti].insert(
        neighbor_reversed_summary.begin(), neighbor_reversed_summary.end());
    core.aux_graph[A].reversed_summary[Ti].insert(top_reversed_summary.begin(),
                                                  top_reversed_summary.end());
    core.aux_graph[A].reversed_summary[Ti].insert(
        trickle_up_reversed_summary.begin(), trickle_up_reversed_summary.end());

    core.BHRS[std::make_pair(A, Ti)].insert(c_reversed_summary.begin(),
                                            c_reversed_summary.end());
    core.BHRS[std::make_pair(A, Ti)].insert(neighbor_reversed_summary.begin(),
                                            neighbor_reversed_summary.end());
  }

  void compute_reversed_summaries(
      Nonterminal A, Nonterminal B, Nonterminal C,
      const std::unordered_map<std::pair<Nonterminal, Transaction>, Transaction>
          &parent,
      const std::unordered_map<std::pair<Nonterminal, Transaction>, Transaction>
          &child) {

    for (const Transaction &Ti : core.aux_graph[A].reversed_vertices) {
      compute_reversed_summary(Ti, A, B, C, parent, child);
    }

    for (const auto &[_, Ti] : core.aux_graph[A].last_transaction) {
      compute_reversed_summary(Ti, A, B, C, parent, child);
    }
  }

  void compute_edge(
      Transaction Ti, Transaction Tj, Nonterminal A, Nonterminal B,
      Nonterminal C,
      const std::unordered_map<std::pair<Nonterminal, Transaction>, Transaction>
          &parent,
      const std::unordered_map<std::pair<Nonterminal, Transaction>, Transaction>
          &child) {

    if (child.contains(std::make_pair(C, Ti)) &&
        child.contains(std::make_pair(C, Tj)) &&
        core.aux_graph[C].edges.contains(
            std::make_pair(child.at(std::make_pair(C, Ti)),
                           child.at(std::make_pair(C, Tj))))) {
      core.aux_graph[A].edges.insert(std::make_pair(Ti, Tj));
      return;
    }

    if (child.contains(std::make_pair(B, Ti)) &&
        child.contains(std::make_pair(B, Tj)) &&
        core.aux_graph[B].edges.contains(
            std::make_pair(child.at(std::make_pair(B, Ti)),
                           child.at(std::make_pair(B, Tj))))) {
      core.aux_graph[A].edges.insert(std::make_pair(Ti, Tj));
      return;
    }

    if (Ti != Tj && child.contains(std::make_pair(B, Ti)) &&
        child.contains(std::make_pair(C, Tj))) {
      for (const Event &ei :
           core.aux_graph[B].content[child.at(std::make_pair(B, Ti))]) {
        for (const Event &ej :
             core.aux_graph[C].content[child.at(std::make_pair(C, Tj))]) {
          if (ei.conflict(ej)) {
            core.aux_graph[A].edges.insert(std::make_pair(Ti, Tj));
            return;
          }
        }
      }
    }

    if (child.contains(std::make_pair(B, Ti)) &&
        child.contains(std::make_pair(C, Tj))) {
      for (const auto &[_, Tk] : core.aux_graph[C].first_transaction) {
        if (parent.contains(std::make_pair(C, Tk)) &&
            Ti != parent.at(std::make_pair(C, Tk)) &&
            contains_event(
                core.aux_graph[C].content[Tk],
                Event{"", EventType::end, "", Annotation::undefined})) {
          for (const Event &ei :
               core.aux_graph[B].content[child.at(std::make_pair(B, Ti))]) {
            for (const Event &ek : core.aux_graph[C].content[Tk]) {
              if (ei.conflict(ek) && (Tk.thread == Tj.thread ||
                                      core.aux_graph[C].reachable(
                                          Tk, child.at(std::make_pair(C, Tj)),
                                          core.aux_graph[C].vertices))) {
                core.aux_graph[A].edges.insert(std::make_pair(Ti, Tj));
                return;
              }
            }
          }
        }
      }
    }

    if (child.contains(std::make_pair(C, Tj))) {
      for (const Event &ei : core.THS[std::make_pair(A, Ti)]) {
        for (const Event &ej :
             core.aux_graph[C].content[child.at(std::make_pair(C, Tj))]) {
          if (ei.conflict(ej)) {
            core.aux_graph[A].edges.insert(std::make_pair(Ti, Tj));
            return;
          }
        }
      }
    }

    if (child.contains(std::make_pair(C, Tj))) {
      for (const auto &[_, Tk] : core.aux_graph[C].first_transaction) {
        if (contains_event(
                core.aux_graph[C].content[Tk],
                Event{"", EventType::end, "", Annotation::undefined})) {
          for (const Event &ei : core.THS[std::make_pair(A, Ti)]) {
            for (const Event &ek : core.aux_graph[C].content[Tk]) {
              if (ei.conflict(ek) && (Tk.thread == Tj.thread ||
                                      core.aux_graph[C].reachable(
                                          Tk, child.at(std::make_pair(C, Tj)),
                                          core.aux_graph[C].vertices))) {
                core.aux_graph[A].edges.insert(std::make_pair(Ti, Tj));
                return;
              }
            }
          }
        }
      }
    }

    if (child.contains(std::make_pair(B, Tj))) {
      for (const Event &ei : core.THS[std::make_pair(A, Ti)]) {
        Transaction Tk = core.aux_graph[B].last_transaction[std::make_tuple(
            ei.thread, EventType::undefined, "")];

        if (Tk.thread == Tj.thread) {
          core.aux_graph[A].edges.insert(std::make_pair(Ti, Tj));
          return;
        }

        if (parent.contains(std::make_pair(B, Tk)) &&
            core.aux_graph[A].content.contains(
                parent.at(std::make_pair(B, Tk))) &&
            contains_event(
                core.aux_graph[A].content[parent.at(std::make_pair(B, Tk))],
                Event{"", EventType::end, "", Annotation::undefined})) {
          if (core.aux_graph[B].edges.contains(
                  std::make_pair(Tk, child.at(std::make_pair(B, Tj))))) {
            core.aux_graph[A].edges.insert(std::make_pair(Ti, Tj));
            return;
          }
        }
      }
    }
  }

  void compute_edges(
      Nonterminal A, Nonterminal B, Nonterminal C,
      const std::unordered_map<std::pair<Nonterminal, Transaction>, Transaction>
          &parent,
      const std::unordered_map<std::pair<Nonterminal, Transaction>, Transaction>
          &child) {

    for (const Transaction &Ti : core.aux_graph[A].vertices) {
      for (const Transaction &Tj : core.aux_graph[A].vertices) {
        compute_edge(Ti, Tj, A, B, C, parent, child);
      }
    }

    for (const auto &[_, Ti] : core.aux_graph[A].first_transaction) {
      if (core.aux_graph[A].vertices.contains(Ti)) {
        continue;
      }
      for (const Transaction &Tj : core.aux_graph[A].vertices) {
        compute_edge(Ti, Tj, A, B, C, parent, child);
      }
    }
  }

  void compute_reversed_edge(
      Transaction Tj, Transaction Ti, Nonterminal A, Nonterminal B,
      Nonterminal C,
      const std::unordered_map<std::pair<Nonterminal, Transaction>, Transaction>
          &parent,
      const std::unordered_map<std::pair<Nonterminal, Transaction>, Transaction>
          &child) {

    if (child.contains(std::make_pair(B, Tj)) &&
        child.contains(std::make_pair(B, Ti)) &&
        core.aux_graph[B].edges.contains(
            std::make_pair(child.at(std::make_pair(B, Tj)),
                           child.at(std::make_pair(B, Ti))))) {
      core.aux_graph[A].edges.insert(std::make_pair(Tj, Ti));
      return;
    }

    if (child.contains(std::make_pair(C, Tj)) &&
        child.contains(std::make_pair(C, Ti)) &&
        core.aux_graph[C].edges.contains(
            std::make_pair(child.at(std::make_pair(C, Tj)),
                           child.at(std::make_pair(C, Ti))))) {
      core.aux_graph[A].edges.insert(std::make_pair(Tj, Ti));
      return;
    }

    if (Tj != Ti && child.contains(std::make_pair(B, Tj)) &&
        child.contains(std::make_pair(C, Ti))) {
      for (const Event &ej :
           core.aux_graph[B].content[child.at(std::make_pair(B, Tj))]) {
        for (const Event &ei :
             core.aux_graph[C].content[child.at(std::make_pair(C, Ti))]) {
          if (ej.conflict(ei)) {
            core.aux_graph[A].edges.insert(std::make_pair(Tj, Ti));
            return;
          }
        }
      }
    }

    if (child.contains(std::make_pair(C, Ti)) &&
        child.contains(std::make_pair(B, Tj))) {
      for (const auto &[_, Tk] : core.aux_graph[B].last_transaction) {
        if (parent.contains(std::make_pair(B, Tk)) &&
            Ti != parent.at(std::make_pair(B, Tk)) &&
            contains_event(
                core.aux_graph[B].content[Tk],
                Event{"", EventType::begin, "", Annotation::undefined})) {
          for (const Event &ek : core.aux_graph[B].content[Tk]) {
            for (const Event &ei :
                 core.aux_graph[C].content[child.at(std::make_pair(C, Ti))]) {
              if (ek.conflict(ei) &&
                  (Tk.thread == Tj.thread ||
                   core.aux_graph[B].reachable(
                       child.at(std::make_pair(B, Tj)), Tk,
                       core.aux_graph[B].reversed_vertices))) {
                core.aux_graph[A].edges.insert(std::make_pair(Tj, Ti));
                return;
              }
            }
          }
        }
      }
    }

    if (child.contains(std::make_pair(B, Tj))) {
      for (const Event &ej :
           core.aux_graph[B].content[child.at(std::make_pair(B, Tj))]) {
        for (const Event &ei : core.BHRS[std::make_pair(A, Ti)]) {
          if (ej.conflict(ei)) {
            core.aux_graph[A].edges.insert(std::make_pair(Tj, Ti));
            return;
          }
        }
      }
    }

    if (child.contains(std::make_pair(B, Tj))) {
      for (const auto &[_, Tk] : core.aux_graph[B].last_transaction) {
        if (contains_event(
                core.aux_graph[B].content[Tk],
                Event{"", EventType::begin, "", Annotation::undefined})) {
          for (const Event &ei : core.BHRS[std::make_pair(A, Ti)]) {
            for (const Event &ek : core.aux_graph[B].content[Tk]) {
              if (ek.conflict(ei) &&
                  (Tk.thread == Tj.thread ||
                   core.aux_graph[B].reachable(
                       child.at(std::make_pair(B, Tj)), Tk,
                       core.aux_graph[B].reversed_vertices))) {
                core.aux_graph[A].edges.insert(std::make_pair(Tj, Ti));
                return;
              }
            }
          }
        }
      }
    }

    if (child.contains(std::make_pair(C, Tj))) {
      for (const Event &ei : core.BHRS[std::make_pair(A, Ti)]) {
        Transaction Tk = core.aux_graph[C].first_transaction[std::make_tuple(
            ei.thread, EventType::undefined, "")];

        if (Tk.thread == Tj.thread) {
          core.aux_graph[A].edges.insert(std::make_pair(Tj, Ti));
          return;
        }

        if (parent.contains(std::make_pair(C, Tk)) &&
            core.aux_graph[A].content.contains(
                parent.at(std::make_pair(C, Tk))) &&
            contains_event(
                core.aux_graph[A].content[parent.at(std::make_pair(C, Tk))],
                Event{"", EventType::begin, "", Annotation::undefined})) {
          if (core.aux_graph[C].edges.contains(
                  std::make_pair(child.at(std::make_pair(C, Tj)), Tk))) {
            core.aux_graph[A].edges.insert(std::make_pair(Tj, Ti));
            return;
          }
        }
      }
    }
  }

  void compute_reversed_edges(
      Nonterminal A, Nonterminal B, Nonterminal C,
      const std::unordered_map<std::pair<Nonterminal, Transaction>, Transaction>
          &parent,
      const std::unordered_map<std::pair<Nonterminal, Transaction>, Transaction>
          &child) {

    for (const Transaction &Ti : core.aux_graph[A].reversed_vertices) {
      for (const Transaction &Tj : core.aux_graph[A].reversed_vertices) {
        compute_reversed_edge(Tj, Ti, A, B, C, parent, child);
      }
    }

    for (const auto &[_, Ti] : core.aux_graph[A].last_transaction) {
      if (core.aux_graph[A].reversed_vertices.contains(Ti)) {
        continue;
      }
      for (const Transaction &Tj : core.aux_graph[A].reversed_vertices) {
        compute_reversed_edge(Tj, Ti, A, B, C, parent, child);
      }
    }
  }

  void refresh_aux_graph(Nonterminal B, Nonterminal C) {

#ifdef DEBUG
    std::cout << "Engine: refresh_aux_graph (" << B << ", " << C << ")\n";
#endif

    std::unordered_map<Transaction, Transaction> fresh;

    for (const auto &txn : core.aux_graph[B].vertices) {
      if (!fresh.contains(txn)) {
        fresh[txn] = make_transaction();
        fresh[txn].thread = txn.thread;
      }
      core.aux_graph[C].vertices.insert(fresh[txn]);
    }

    for (const auto &txn : core.aux_graph[B].reversed_vertices) {
      if (!fresh.contains(txn)) {
        fresh[txn] = make_transaction();
        fresh[txn].thread = txn.thread;
      }
      core.aux_graph[C].reversed_vertices.insert(fresh[txn]);
    }

    for (const auto &[decor, txn] : core.aux_graph[B].first_transaction) {
      if (!fresh.contains(txn)) {
        fresh[txn] = make_transaction();
        fresh[txn].thread = txn.thread;
      }
      core.aux_graph[C].first_transaction[decor] = fresh[txn];
    }

    for (const auto &[decor, txn] : core.aux_graph[B].last_transaction) {
      if (!fresh.contains(txn)) {
        fresh[txn] = make_transaction();
        fresh[txn].thread = txn.thread;
      }
      core.aux_graph[C].last_transaction[decor] = fresh[txn];
    }

    for (const auto &[Ti, Tj] : core.aux_graph[B].edges) {
      core.aux_graph[C].edges.insert(
          std::make_pair(fresh.at(Ti), fresh.at(Tj)));
    }

    for (const auto &[txn, _] : core.aux_graph[B].content) {
      if (!fresh.contains(txn)) {
        fresh[txn] = make_transaction();
        fresh[txn].thread = txn.thread;
      }
      core.aux_graph[C].content[fresh.at(txn)] = core.aux_graph[B].content[txn];
    }

    for (const auto &[txn, _] : core.aux_graph[B].summary) {
      if (!fresh.contains(txn)) {
        fresh[txn] = make_transaction();
        fresh[txn].thread = txn.thread;
      }
      core.aux_graph[C].summary[fresh.at(txn)] = core.aux_graph[B].summary[txn];
    }

    for (const auto &[txn, _] : core.aux_graph[B].reversed_summary) {
      if (!fresh.contains(txn)) {
        fresh[txn] = make_transaction();
        fresh[txn].thread = txn.thread;
      }
      core.aux_graph[C].reversed_summary[fresh.at(txn)] =
          core.aux_graph[B].reversed_summary[txn];
    }
  }

  void compute_aux_graph_inductive(Nonterminal A, Nonterminal B,
                                   Nonterminal C) {

    if (B == C) {
      C += "'";
      refresh_aux_graph(B, C);
    }

#ifdef DEBUG
    std::cout << "Engine: compute_aux_graph (" << A << " -> " << B << " " << C
              << ")\n";
#endif

    std::unordered_map<std::pair<Nonterminal, Transaction>, Transaction> parent;
    std::unordered_map<std::pair<Nonterminal, Transaction>, Transaction> child;

#ifdef DEBUG
    std::cout << "Before merge_routine\n";
    std::cout << core.aux_graph[A] << "\n";
#endif

    merge_routine(A, B, C, parent, child);

#ifdef DEBUG
    std::cout << "After merge_routine\n";
    std::cout << core.aux_graph[A] << "\n";
#endif

    compute_vertices(A, B, C, parent, child);

#ifdef DEBUG
    std::cout << "After compute_vertices\n";
    std::cout << core.aux_graph[A] << "\n";
#endif

    compute_reversed_vertices(A, B, C, parent, child);

#ifdef DEBUG
    std::cout << "After compute_reversed_vertices\n";
    std::cout << core.aux_graph[A] << "\n";
#endif

    compute_first_transactions(A, B, C, parent, child);

#ifdef DEBUG
    std::cout << "After compute_first_transactions\n";
    std::cout << core.aux_graph[A] << "\n";
#endif

    compute_last_transactions(A, B, C, parent, child);

#ifdef DEBUG
    std::cout << "After compute_last_transactions\n";
    std::cout << core.aux_graph[A] << "\n";
#endif

    compute_summaries(A, B, C, parent, child);

#ifdef DEBUG
    std::cout << "After compute_summaries\n";
    std::cout << core.aux_graph[A] << "\n";
#endif

    compute_reversed_summaries(A, B, C, parent, child);

#ifdef DEBUG
    std::cout << "After compute_reversed_summaries\n";
    std::cout << core.aux_graph[A] << "\n";
#endif

    compute_edges(A, B, C, parent, child);

#ifdef DEBUG
    std::cout << "After compute_edges\n";
    std::cout << core.aux_graph[A] << "\n";
#endif

    compute_reversed_edges(A, B, C, parent, child);

#ifdef DEBUG
    std::cout << "After compute_reversed_edges\n";
    std::cout << core.aux_graph[A] << "\n";
#endif
  }

  bool compute_aux_graph_base_forward(
      Nonterminal A, std::vector<Event> events,
      std::unordered_map<Thread, std::vector<Transaction>> &transactions) {

    std::unordered_map<Thread, Transaction> alive;

    Event event = events[0];

    Transaction txn = make_transaction();
    txn.thread = event.thread;
    transactions[txn.thread].push_back(txn);

    if (event.type == EventType::begin) {
      event.annotation = Annotation::down;
    } else if (event.type == EventType::end) {
      event.annotation = Annotation::up;
    } else {
      event.annotation = Annotation::open;
    }

    if (event.type != EventType::end) {
      alive[txn.thread] = txn;
      core.aux_graph[A].vertices.insert(txn);
    }

    core.aux_graph[A].first_transaction[std::make_tuple(
        event.thread, EventType::undefined, "")] = txn;

    core.aux_graph[A].first_transaction[std::make_tuple(
        event.thread, event.type, event.operand)] = txn;

    core.aux_graph[A].content[txn].insert(event);

    for (size_t i = 1; i < events.size(); i++) {

      event = events[i];

      if (event.type == EventType::read || event.type == EventType::write ||
          event.type == EventType::lock || event.type == EventType::unlock ||
          event.type == EventType::fork || event.type == EventType::join) {

        if (alive.contains(event.thread)) {
          txn = alive[event.thread];
          event.annotation = core.aux_graph[A].content[txn].begin()->annotation;
        } else {
          txn = make_transaction();
          txn.thread = event.thread;
          transactions[txn.thread].push_back(txn);
          alive[txn.thread] = txn;
          core.aux_graph[A].vertices.insert(txn);
          event.annotation = Annotation::open;
        }

        if (!core.aux_graph[A].first_transaction.contains(
                std::make_tuple(event.thread, EventType::undefined, ""))) {
          core.aux_graph[A].first_transaction[std::make_tuple(
              event.thread, EventType::undefined, "")] = txn;
        }

        if (!core.aux_graph[A].first_transaction.contains(
                std::make_tuple(event.thread, event.type, event.operand))) {
          core.aux_graph[A].first_transaction[std::make_tuple(
              event.thread, event.type, event.operand)] = txn;
        }

        core.aux_graph[A].content[txn].insert(event);

        // For optimization
        std::unordered_set<Transaction> checked;

        // Add v->v edges
        for (const auto &v : core.aux_graph[A].vertices) {
          if (v.thread != event.thread) {
            for (const auto &e : core.aux_graph[A].content[v]) {
              if (e.conflict(event)) {
                core.aux_graph[A].edges.insert(std::make_pair(v, txn));
              }
            }
            for (const auto &e : core.aux_graph[A].summary[v]) {
              if (e.conflict(event)) {
                core.aux_graph[A].edges.insert(std::make_pair(v, txn));
              }
            }
            checked.insert(v);
          }
        }

        // Add f->v edges
        for (const auto &[_, f] : core.aux_graph[A].first_transaction) {
          if (f != txn) {
            if (checked.contains(f)) {
              continue;
            }
            for (const auto &e : core.aux_graph[A].content[f]) {
              if (e.conflict(event)) {
                core.aux_graph[A].edges.insert(std::make_pair(f, txn));
              }
            }
            for (const auto &e : core.aux_graph[A].summary[f]) {
              if (e.conflict(event)) {
                core.aux_graph[A].edges.insert(std::make_pair(f, txn));
              }
            }
            checked.insert(f);
          }
        }

      } else if (event.type == EventType::begin) {

        txn = make_transaction();
        txn.thread = event.thread;
        transactions[txn.thread].push_back(txn);

        event.annotation = Annotation::down;

        alive[txn.thread] = txn;
        core.aux_graph[A].vertices.insert(txn);

        if (!core.aux_graph[A].first_transaction.contains(
                std::make_tuple(event.thread, EventType::undefined, ""))) {
          core.aux_graph[A].first_transaction[std::make_tuple(
              event.thread, EventType::undefined, "")] = txn;
        }

        if (!core.aux_graph[A].first_transaction.contains(
                std::make_tuple(event.thread, event.type, event.operand))) {
          core.aux_graph[A].first_transaction[std::make_tuple(
              event.thread, event.type, event.operand)] = txn;
        }

        core.aux_graph[A].content[txn].insert(event);

        // TODO: Edges computation can probably be optimized away?

        // For optimization
        std::unordered_set<Transaction> checked;

        // Add v->v edges
        for (const auto &v : core.aux_graph[A].vertices) {
          if (v.thread != event.thread) {
            for (const auto &e : core.aux_graph[A].content[v]) {
              if (e.conflict(event)) {
                core.aux_graph[A].edges.insert(std::make_pair(v, txn));
              }
            }
            for (const auto &e : core.aux_graph[A].summary[v]) {
              if (e.conflict(event)) {
                core.aux_graph[A].edges.insert(std::make_pair(v, txn));
              }
            }
            checked.insert(v);
          }
        }

        // Add f->v edges
        for (const auto &[_, f] : core.aux_graph[A].first_transaction) {
          if (f != txn) {
            if (checked.contains(f)) {
              continue;
            }
            for (const auto &e : core.aux_graph[A].content[f]) {
              if (e.conflict(event)) {
                core.aux_graph[A].edges.insert(std::make_pair(f, txn));
              }
            }
            for (const auto &e : core.aux_graph[A].summary[f]) {
              if (e.conflict(event)) {
                core.aux_graph[A].edges.insert(std::make_pair(f, txn));
              }
            }
            checked.insert(f);
          }
        }

      } else if (event.type == EventType::end) {

        if (alive.contains(event.thread)) {
          txn = alive[event.thread];

          Annotation annotation =
              core.aux_graph[A].content[txn].begin()->annotation;
          if (annotation == Annotation::down) {
            event.annotation = Annotation::closed;
          } else if (annotation == Annotation::open) {
            event.annotation = Annotation::up;
          } else {
            std::cerr
                << "compute_aux_graph_base_forward: something went wrong\n";
            std::exit(EXIT_FAILURE);
          }

          // TODO: Should we do FT before or after edges?
          if (!core.aux_graph[A].first_transaction.contains(
                  std::make_tuple(event.thread, EventType::undefined, ""))) {
            core.aux_graph[A].first_transaction[std::make_tuple(
                event.thread, EventType::undefined, "")] = txn;
          }

          if (!core.aux_graph[A].first_transaction.contains(
                  std::make_tuple(event.thread, event.type, event.operand))) {
            core.aux_graph[A].first_transaction[std::make_tuple(
                event.thread, event.type, event.operand)] = txn;
          }

          std::unordered_set<Event> updated_content;
          for (const auto &e : core.aux_graph[A].content[txn]) {
            updated_content.insert(
                Event{e.thread, e.type, e.operand, event.annotation});
          }
          updated_content.insert(event);

          core.aux_graph[A].content[txn] = updated_content;

          std::unordered_set<Transaction> immediate_preds;
          std::unordered_set<Transaction> immediate_succs;

          // Connect v -> v -> v edges

          for (const auto &v : core.aux_graph[A].vertices) {
            for (const auto &w : core.aux_graph[A].vertices) {
              if (core.aux_graph[A].edges.contains(std::make_pair(v, w))) {
                if (txn == v) {
                  immediate_succs.insert(w);

                  bool remove_edge = true;
                  for (const auto &[_, f] :
                       core.aux_graph[A].first_transaction) {
                    if (txn == f) {
                      remove_edge = false;
                      break;
                    }
                  }

                  // The edge is safe to remove only if it's not an f->v edge
                  if (remove_edge) {
                    core.aux_graph[A].edges.erase(std::make_pair(txn, w));
                  }
                }

                else if (txn == w) {
                  immediate_preds.insert(v);

                  core.aux_graph[A].summary[v].insert(
                      core.aux_graph[A].content[txn].begin(),
                      core.aux_graph[A].content[txn].end());

                  core.aux_graph[A].edges.erase(std::make_pair(v, txn));
                }
              }
            }
          }

          // TODO: Should we detect general cycle too?

          for (const auto &p : immediate_preds) {
            for (const auto &s : immediate_succs) {
              if (p == s) {
                // Self-cycle detected!
                core.csv[A] = true;
                return true;
              }
              core.aux_graph[A].edges.insert(std::make_pair(p, s));
            }
          }

          // Connect f -> v -> v edges

          immediate_preds.clear();

          for (const auto &[_, f] : core.aux_graph[A].first_transaction) {
            for (const auto &w : core.aux_graph[A].vertices) {
              if (core.aux_graph[A].edges.contains(std::make_pair(f, w))) {
                if (txn == w) {
                  immediate_preds.insert(f);

                  core.aux_graph[A].summary[f].insert(
                      core.aux_graph[A].content[txn].begin(),
                      core.aux_graph[A].content[txn].end());

                  core.aux_graph[A].edges.erase(std::make_pair(f, txn));
                }
              }
            }
          }

          for (const auto &p : immediate_preds) {
            for (const auto &s : immediate_succs) {
              if (p == s) {
                // Cycle detected!
                core.csv[A] = true;
                return true;
              }
              core.aux_graph[A].edges.insert(std::make_pair(p, s));
            }
          }

          alive.erase(txn.thread);
          core.aux_graph[A].vertices.erase(txn);

        } else {
          // It's the first event in the thread.

          txn = make_transaction();
          txn.thread = event.thread;
          transactions[txn.thread].push_back(txn);

          event.annotation = Annotation::up;

          if (!core.aux_graph[A].first_transaction.contains(
                  std::make_tuple(event.thread, EventType::undefined, ""))) {
            core.aux_graph[A].first_transaction[std::make_tuple(
                event.thread, EventType::undefined, "")] = txn;
          }

          if (!core.aux_graph[A].first_transaction.contains(
                  std::make_tuple(event.thread, event.type, event.operand))) {
            core.aux_graph[A].first_transaction[std::make_tuple(
                event.thread, event.type, event.operand)] = txn;
          }

          core.aux_graph[A].content[txn].insert(event);
        }

      } else {
        std::cerr << "compute_aux_graph_base_forward: something went wrong\n";
        std::exit(EXIT_FAILURE);
      }
    }

    return false;
  }

  bool compute_aux_graph_base_backward(
      Nonterminal A, std::vector<Event> events,
      std::unordered_map<Thread, std::vector<Transaction>> &transactions) {

    std::reverse(events.begin(), events.end());

    std::unordered_map<Thread, Transaction> alive;

    Event event = events[0];

    Transaction txn = transactions[event.thread].back();

    if (event.type != EventType::begin) {
      alive[txn.thread] = txn;
      core.aux_graph[A].reversed_vertices.insert(txn);
    }

    core.aux_graph[A].last_transaction[std::make_tuple(
        event.thread, EventType::undefined, "")] = txn;

    core.aux_graph[A].last_transaction[std::make_tuple(event.thread, event.type,
                                                       event.operand)] = txn;

    if (event.type == EventType::begin) {
      transactions[event.thread].pop_back();
    }

    for (size_t i = 1; i < events.size(); i++) {

      event = events[i];

      if (event.type == EventType::read || event.type == EventType::write ||
          event.type == EventType::lock || event.type == EventType::unlock ||
          event.type == EventType::fork || event.type == EventType::join) {

        if (alive.contains(event.thread)) {
          txn = alive[event.thread];
        } else {
          txn = transactions[event.thread].back();
          alive[txn.thread] = txn;
          core.aux_graph[A].reversed_vertices.insert(txn);
        }

        if (!core.aux_graph[A].last_transaction.contains(
                std::make_tuple(event.thread, EventType::undefined, ""))) {
          core.aux_graph[A].last_transaction[std::make_tuple(
              event.thread, EventType::undefined, "")] = txn;
        }

        if (!core.aux_graph[A].last_transaction.contains(
                std::make_tuple(event.thread, event.type, event.operand))) {
          core.aux_graph[A].last_transaction[std::make_tuple(
              event.thread, event.type, event.operand)] = txn;
        }

        // For optimization
        std::unordered_set<Transaction> checked;

        // Add rv->rv edges
        for (const auto &v : core.aux_graph[A].reversed_vertices) {
          if (v.thread != event.thread) {
            for (const auto &e : core.aux_graph[A].content[v]) {
              if (event.conflict(e)) {
                core.aux_graph[A].edges.insert(std::make_pair(txn, v));
              }
            }
            for (const auto &e : core.aux_graph[A].reversed_summary[v]) {
              if (event.conflict(e)) {
                core.aux_graph[A].edges.insert(std::make_pair(txn, v));
              }
            }
            checked.insert(v);
          }
        }

        // Add rv->l edges
        for (const auto &[_, l] : core.aux_graph[A].last_transaction) {
          if (l != txn) {
            if (checked.contains(l)) {
              continue;
            }
            for (const auto &e : core.aux_graph[A].content[l]) {
              if (event.conflict(e)) {
                core.aux_graph[A].edges.insert(std::make_pair(txn, l));
              }
            }
            for (const auto &e : core.aux_graph[A].reversed_summary[l]) {
              if (event.conflict(e)) {
                core.aux_graph[A].edges.insert(std::make_pair(txn, l));
              }
            }
            checked.insert(l);
          }
        }

      } else if (event.type == EventType::end) {

        txn = transactions[event.thread].back();

        alive[txn.thread] = txn;
        core.aux_graph[A].reversed_vertices.insert(txn);

        if (!core.aux_graph[A].last_transaction.contains(
                std::make_tuple(event.thread, EventType::undefined, ""))) {
          core.aux_graph[A].last_transaction[std::make_tuple(
              event.thread, EventType::undefined, "")] = txn;
        }

        if (!core.aux_graph[A].last_transaction.contains(
                std::make_tuple(event.thread, event.type, event.operand))) {
          core.aux_graph[A].last_transaction[std::make_tuple(
              event.thread, event.type, event.operand)] = txn;
        }

        // For optimization
        std::unordered_set<Transaction> checked;

        // Add rv->rv edges
        for (const auto &v : core.aux_graph[A].reversed_vertices) {
          if (v.thread != event.thread) {
            for (const auto &e : core.aux_graph[A].content[v]) {
              if (event.conflict(e)) {
                core.aux_graph[A].edges.insert(std::make_pair(txn, v));
              }
            }
            for (const auto &e : core.aux_graph[A].reversed_summary[v]) {
              if (event.conflict(e)) {
                core.aux_graph[A].edges.insert(std::make_pair(txn, v));
              }
            }
            checked.insert(v);
          }
        }

        // Add rv->l edges
        for (const auto &[_, l] : core.aux_graph[A].last_transaction) {
          if (l != txn) {
            if (checked.contains(l)) {
              continue;
            }
            for (const auto &e : core.aux_graph[A].content[l]) {
              if (event.conflict(e)) {
                core.aux_graph[A].edges.insert(std::make_pair(txn, l));
              }
            }
            for (const auto &e : core.aux_graph[A].reversed_summary[l]) {
              if (event.conflict(e)) {
                core.aux_graph[A].edges.insert(std::make_pair(txn, l));
              }
            }
            checked.insert(l);
          }
        }

      } else if (event.type == EventType::begin) {

        if (alive.contains(event.thread)) {
          txn = alive[event.thread];

          if (!core.aux_graph[A].last_transaction.contains(
                  std::make_tuple(event.thread, EventType::undefined, ""))) {
            core.aux_graph[A].last_transaction[std::make_tuple(
                event.thread, EventType::undefined, "")] = txn;
          }

          if (!core.aux_graph[A].last_transaction.contains(
                  std::make_tuple(event.thread, event.type, event.operand))) {
            core.aux_graph[A].last_transaction[std::make_tuple(
                event.thread, event.type, event.operand)] = txn;
          }

          std::unordered_set<Transaction> immediate_preds;
          std::unordered_set<Transaction> immediate_succs;

          // Connect rv -> rv -> rv edges

          for (const auto &v : core.aux_graph[A].reversed_vertices) {
            for (const auto &w : core.aux_graph[A].reversed_vertices) {
              if (core.aux_graph[A].edges.contains(std::make_pair(v, w))) {
                if (txn == v) {
                  immediate_succs.insert(w);

                  core.aux_graph[A].reversed_summary[w].insert(
                      core.aux_graph[A].content[txn].begin(),
                      core.aux_graph[A].content[txn].end());

                  core.aux_graph[A].edges.erase(std::make_pair(txn, w));
                }

                else if (txn == w) {
                  immediate_preds.insert(v);

                  bool remove_edge = true;
                  for (const auto &[_, l] :
                       core.aux_graph[A].last_transaction) {
                    if (txn == l) {
                      remove_edge = false;
                      break;
                    }
                  }

                  // The edge is safe to remove only if it's not an rv->l edge
                  if (remove_edge) {
                    core.aux_graph[A].edges.erase(std::make_pair(v, txn));
                  }
                }
              }
            }
          }

          // TODO: Should we detect general cycle too?

          for (const auto &p : immediate_preds) {
            for (const auto &s : immediate_succs) {
              core.aux_graph[A].edges.insert(std::make_pair(p, s));
            }
          }

          // Connect rv -> rv -> l edges

          immediate_succs.clear();

          for (const auto &[_, l] : core.aux_graph[A].last_transaction) {
            for (const auto &w : core.aux_graph[A].reversed_vertices) {
              if (core.aux_graph[A].edges.contains(std::make_pair(w, l))) {
                if (txn == w) {
                  immediate_succs.insert(l);

                  core.aux_graph[A].reversed_summary[l].insert(
                      core.aux_graph[A].content[txn].begin(),
                      core.aux_graph[A].content[txn].end());

                  core.aux_graph[A].edges.erase(std::make_pair(txn, l));
                }
              }
            }
          }

          for (const auto &p : immediate_preds) {
            for (const auto &s : immediate_succs) {
              core.aux_graph[A].edges.insert(std::make_pair(p, s));
            }
          }

          alive.erase(txn.thread);
          core.aux_graph[A].reversed_vertices.erase(txn);

          transactions[event.thread].pop_back();

        } else {
          // It's the last event in the thread.

          txn = transactions[event.thread].back();

          if (!core.aux_graph[A].last_transaction.contains(
                  std::make_tuple(event.thread, EventType::undefined, ""))) {
            core.aux_graph[A].last_transaction[std::make_tuple(
                event.thread, EventType::undefined, "")] = txn;
          }

          if (!core.aux_graph[A].last_transaction.contains(
                  std::make_tuple(event.thread, event.type, event.operand))) {
            core.aux_graph[A].last_transaction[std::make_tuple(
                event.thread, event.type, event.operand)] = txn;
          }

          transactions[event.thread].pop_back();
        }

      } else {
        std::cerr << "compute_aux_graph_base_backward: something went wrong\n";
        std::exit(EXIT_FAILURE);
      }
    }

    return false;
  }

  void compute_aux_graph_base(Nonterminal A, std::vector<Terminal> terminals) {

#ifdef DEBUG
    std::cout << "Engine: compute_aux_graph (" << A << " ->";
    for (const auto &term : terminals) {
      std::cout << " " << term;
    }
    std::cout << ")\n";
#endif

    std::vector<Event> events;
    for (const auto &term : terminals) {
      events.push_back(core.grammar.content[term]);
    }

    std::unordered_map<Thread, std::vector<Transaction>> transactions;

    if (compute_aux_graph_base_forward(A, events, transactions)) {
#ifdef DEBUG
      std::cout << "cycle detected!\n";
      std::cout << "forward\n";
      std::cout << core.aux_graph[A] << "\n";
      std::exit(EXIT_FAILURE);
#endif
      return;
    }

    // Copy forward edges as it may be erased on computing aux graph backward
    std::unordered_set<std::pair<Transaction, Transaction>> forward_edges =
        core.aux_graph[A].edges;

#ifdef DEBUG
    std::cout << "forward\n";
    std::cout << core.aux_graph[A] << "\n";
#endif

    if (compute_aux_graph_base_backward(A, events, transactions)) {
      return;
    }

    core.aux_graph[A].edges.insert(forward_edges.begin(), forward_edges.end());

#ifdef DEBUG
    std::cout << "forward & backward\n";
    std::cout << core.aux_graph[A] << "\n";
#endif
  }

  void reduce_aux_graph_base(Nonterminal nonterminal) {
    std::unordered_set<std::tuple<Thread, EventType, Operand>> to_erase;

    for (const auto &[decor, txn] :
         core.aux_graph[nonterminal].first_transaction) {
      EventType type = std::get<1>(decor);
      if (type == EventType::read || type == EventType::write ||
          type == EventType::lock || type == EventType::unlock) {
        Operand operand = std::get<2>(decor);
        if (!relv.maintain[nonterminal].contains(operand)) {
          to_erase.insert(decor);
        }
      }
    }

    for (const auto &decor : to_erase) {
      core.aux_graph[nonterminal].first_transaction.erase(decor);
    }

    to_erase.clear();

    for (const auto &[decor, txn] :
         core.aux_graph[nonterminal].last_transaction) {
      EventType type = std::get<1>(decor);
      if (type == EventType::read || type == EventType::write ||
          type == EventType::lock || type == EventType::unlock) {
        Operand operand = std::get<2>(decor);
        if (!relv.maintain[nonterminal].contains(operand)) {
          to_erase.insert(decor);
        }
      }
    }

    for (const auto &decor : to_erase) {
      core.aux_graph[nonterminal].last_transaction.erase(decor);
    }

#ifdef DEBUG
    std::cout << "base reduction\n";
    std::cout << core.aux_graph[nonterminal] << "\n\n";
#endif
  }

  void compute_aux_graph(Nonterminal nonterminal) {
    if (core.aux_graph.contains(nonterminal)) {
      return;
    }

    std::vector<Symbol> chunks = core.grammar.rules[nonterminal];

    if (core.grammar.terminals.contains(chunks[0])) {
      compute_aux_graph_base(nonterminal, chunks);
      if (!core.csv[nonterminal]) {
        reduce_aux_graph_base(nonterminal);
      }
    } else {
      compute_aux_graph_inductive(nonterminal, chunks[0], chunks[1]);
    }
  }

  void compute_cross_graph(Nonterminal A) {
    if (core.cross_graph.contains(A)) {
      return;
    }

    std::vector<Nonterminal> chunks = core.grammar.rules[A];
    if (chunks.size() != 2) {
      std::cerr << "compute_cross_graph: something went wrong\n";
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

    for (const Transaction &vertex : core.aux_graph[B].vertices) {

      cross_transaction[vertex.thread] = make_transaction();
      cross_transaction[vertex.thread].thread = vertex.thread;

      content[std::make_pair(B, cross_transaction[vertex.thread])] =
          core.aux_graph[B].content[vertex];
      summary[std::make_pair(B, cross_transaction[vertex.thread])] =
          core.aux_graph[B].summary[vertex];
    }

    for (const Transaction &reversed_vertex :
         core.aux_graph[C].reversed_vertices) {

      if (cross_transaction.contains(reversed_vertex.thread)) {

        if (cross_transaction[reversed_vertex.thread].thread !=
            reversed_vertex.thread) {
          std::cerr << "compute_aux_graph: something went wrong\n";
          std::exit(EXIT_FAILURE);
        }

        content[std::make_pair(C, cross_transaction[reversed_vertex.thread])] =
            core.aux_graph[C].content[reversed_vertex];
        reversed_summary[std::make_pair(
            C, cross_transaction[reversed_vertex.thread])] =
            core.aux_graph[C].reversed_summary[reversed_vertex];

      } else {

        cross_transaction[reversed_vertex.thread] = make_transaction();
        cross_transaction[reversed_vertex.thread].thread =
            reversed_vertex.thread;

        content[std::make_pair(C, cross_transaction[reversed_vertex.thread])] =
            core.aux_graph[C].content[reversed_vertex];
        reversed_summary[std::make_pair(
            C, cross_transaction[reversed_vertex.thread])] =
            core.aux_graph[C].reversed_summary[reversed_vertex];
      }
    }

    for (const std::pair<const Thread, Transaction> &it : cross_transaction) {
      core.cross_graph[A].vertices.insert(it.second);
    }

    for (const std::pair<Transaction, Transaction> &edge :
         core.aux_graph[B].edges) {
      if (core.aux_graph[B].vertices.contains(edge.first) &&
          core.aux_graph[B].vertices.contains(edge.second)) {

        core.cross_graph[A].edges.insert(
            std::make_pair(cross_transaction[edge.first.thread],
                           cross_transaction[edge.second.thread]));
      }
    }

    for (const std::pair<Transaction, Transaction> &edge :
         core.aux_graph[C].edges) {
      if (core.aux_graph[C].reversed_vertices.contains(edge.first) &&
          core.aux_graph[C].reversed_vertices.contains(edge.second)) {

        core.cross_graph[A].edges.insert(
            std::make_pair(cross_transaction[edge.first.thread],
                           cross_transaction[edge.second.thread]));
      }
    }

    for (const Transaction &v : core.cross_graph[A].vertices) {
      for (const Transaction &w : core.cross_graph[A].vertices) {

        if (v != w) {
          for (const Event &e : content[std::make_pair(B, v)]) {
            for (const Event &f : content[std::make_pair(C, w)]) {
              if (e.conflict(f)) {
                core.cross_graph[A].edges.insert(std::make_pair(v, w));
              }
            }
          }
        }

        for (const Event &e : content[std::make_pair(B, v)]) {
          for (const Event &f : reversed_summary[std::make_pair(C, w)]) {
            if (e.conflict(f)) {
              core.cross_graph[A].edges.insert(std::make_pair(v, w));
            }
          }
        }

        for (const Event &e : summary[std::make_pair(B, v)]) {
          for (const Event &f : content[std::make_pair(C, w)]) {
            if (e.conflict(f)) {
              core.cross_graph[A].edges.insert(std::make_pair(v, w));
            }
          }
        }

        for (const Event &e : summary[std::make_pair(B, v)]) {
          for (const Event &f : reversed_summary[std::make_pair(C, w)]) {
            if (e.conflict(f)) {
              core.cross_graph[A].edges.insert(std::make_pair(v, w));
            }
          }
        }
      }
    }
  }

  bool analyze_csv(Nonterminal nonterminal) {

#ifdef DEBUG
    std::cout << "Engine: analyze_csv(" + nonterminal + ")\n";
    core.grammar.generate(nonterminal);
    std::cout << "\n";
#endif

    if (core.grammar.terminals.contains(
            core.grammar.rules[nonterminal].front())) {

      core.csv[nonterminal] = false;
      compute_aux_graph(nonterminal);
      // If aux base computation detects a cycle, csv is set to true.
      return core.csv[nonterminal];

    } else {

      compute_cross_graph(nonterminal);

#ifdef DEBUG
      std::cout << "Cross graph:\n";
      std::cout << core.cross_graph[nonterminal] << "\n";
#endif

      bool violation = core.cross_graph[nonterminal].cyclic();
      core.csv[nonterminal] = violation;
      if (!violation) {
        compute_aux_graph(nonterminal);
      }
      return violation;
    }
  }

  void compute_lowest_occurences(Nonterminal nonterminal) {
    for (const Terminal &term : core.grammar.rules[nonterminal]) {
      Event event = core.grammar.content[term];
      // For now we keep track of variables and locks.
      // TODO: Maybe threads as well?
      if (event.type == EventType::read || event.type == EventType::write ||
          event.type == EventType::lock || event.type == EventType::unlock) {
        relv.operands[nonterminal].insert(event.operand);
        relv.lowest_occurences[event.operand].insert(nonterminal);
      }
    }
  }

  void compute_parents_dfs(Nonterminal vertex,
                           std::unordered_set<Nonterminal> &visited) {
    visited.insert(vertex);
    for (Symbol neighbor : core.grammar.rules.at(vertex)) {
      if (core.grammar.nonterminals.contains(neighbor)) {
        relv.parents[neighbor].insert(vertex);
        if (!visited.contains(neighbor)) {
          compute_parents_dfs(neighbor, visited);
        }
      }
    }
  }

  void compute_parents() {
    Nonterminal start{"0"};
    std::unordered_set<Nonterminal> visited;
    compute_parents_dfs(start, visited);
  }

  void compute_ancestor_dfs(Nonterminal start, Nonterminal vertex,
                            std::unordered_set<Nonterminal> &visited) {

    visited.insert(vertex);

    if (vertex == "0") {
      return;
    }

    for (const auto &p : relv.parents.at(vertex)) {
      relv.ancestors[start].insert(p);
      if (!visited.contains(p)) {
        compute_ancestor_dfs(start, p, visited);
      }
    }
  }

  void compute_ancestor(Nonterminal start) {

    std::unordered_set<Nonterminal> visited;
    compute_ancestor_dfs(start, start, visited);
  }

  void compute_lowest_common_ancestor_dfs(
      Nonterminal vertex, Nonterminal &lowest_common_ancestor,
      const std::unordered_set<Nonterminal> &common_ancestors) {

    Nonterminal top_child = core.grammar.rules[vertex][0];
    Nonterminal bottom_child = core.grammar.rules[vertex][1];

    bool has_top_child = common_ancestors.contains(top_child);
    bool has_bottom_child = common_ancestors.contains(bottom_child);

    if (has_top_child && has_bottom_child) {
      lowest_common_ancestor = vertex;
    } else if (has_top_child) {
      compute_lowest_common_ancestor_dfs(top_child, lowest_common_ancestor,
                                         common_ancestors);
    } else if (has_bottom_child) {
      compute_lowest_common_ancestor_dfs(bottom_child, lowest_common_ancestor,
                                         common_ancestors);
    } else {
      lowest_common_ancestor = vertex;
    }
  }

  void compute_lowest_common_ancestor(Operand operand) {

    if (relv.lowest_common_ancestor.contains(operand)) {
      return;
    }

    std::unordered_set<Nonterminal> common_ancestors;

    bool first = true;

    for (const auto &nont : relv.lowest_occurences[operand]) {
      if (first) {
        common_ancestors.insert(relv.ancestors.at(nont).begin(),
                                relv.ancestors.at(nont).end());
        first = false;
      } else {
        std::unordered_set<Nonterminal> intersection;
        for (const auto &anct : relv.ancestors.at(nont)) {
          if (common_ancestors.contains(anct)) {
            intersection.insert(anct);
          }
        }
        common_ancestors = intersection;
      }
    }

#ifdef DEBUG
    std::cout << "Lowest occurences of " << operand << "\n";
    for (const auto &nont : relv.lowest_occurences[operand]) {
      std::cout << nont << " ";
    }
    std::cout << "\n";

    // for (const auto &nont : relv.lowest_occurences[operand]) {
    //   std::cout << "Ancestors of " << nont << "\n";
    //   for (const auto &anct : ancestors.at(nont)) {
    //     std::cout << anct << " ";
    //   }
    //   std::cout << "\n\n";
    // }

    std::cout << "Common ancestors of " << operand << "\n";
    for (const auto &anct : common_ancestors) {
      std::cout << anct << " ";
    }
    std::cout << "\n";
#endif

    Nonterminal start{"0"};
    compute_lowest_common_ancestor_dfs(
        start, relv.lowest_common_ancestor[operand], common_ancestors);

#ifdef DEBUG
    std::cout << "Lowest common ancestor of " << operand << " = "
              << relv.lowest_common_ancestor[operand] << "\n\n";
#endif
  }

  void compute_relevance_base(Nonterminal nonterminal) {
    for (const Operand &op : relv.operands[nonterminal]) {
      if (relv.lowest_common_ancestor[op] != nonterminal) {
        relv.maintain[nonterminal].insert(op);
      }
    }

#ifdef DEBUG
    std::cout << "Relevant operands to maintain from " << nonterminal << "\n";
    for (const auto &op : relv.maintain[nonterminal]) {
      std::cout << op << " ";
    }
    std::cout << "\n\n";
#endif
  }

  void compute_relevance_inductive(Nonterminal nonterminal) {
    Nonterminal top_child = core.grammar.rules[nonterminal][0];
    Nonterminal bottom_child = core.grammar.rules[nonterminal][1];

    for (const Operand &op : relv.maintain[top_child]) {
      if (relv.lowest_common_ancestor[op] != nonterminal) {
        relv.maintain[nonterminal].insert(op);
      }
    }

    for (const Operand &op : relv.maintain[bottom_child]) {
      if (relv.lowest_common_ancestor[op] != nonterminal) {
        relv.maintain[nonterminal].insert(op);
      }
    }

#ifdef DEBUG
    std::cout << "Relevant operands to maintain from " << nonterminal << "\n";
    for (const auto &op : relv.maintain[nonterminal]) {
      std::cout << op << " ";
    }
    std::cout << "\n\n";
#endif
  }

  void preprocess() {
    std::unordered_set<Nonterminal> lowest_nonterminals;
    for (const Nonterminal &nonterminal : core.topological_ordering) {
      if (core.grammar.terminals.contains(
              core.grammar.rules[nonterminal].front())) {
        lowest_nonterminals.insert(nonterminal);
      }
    }

    // 1. Compute lowest occurences of operands
    for (const Nonterminal &nonterminal : lowest_nonterminals) {
      compute_lowest_occurences(nonterminal);
    }

    // 2. Compute lowest common ancestor
    // std::unordered_map<Nonterminal, std::unordered_set<Nonterminal>> parents;
    compute_parents();

    // std::unordered_map<Nonterminal, std::unordered_set<Nonterminal>>
    // ancestors;
    for (const Nonterminal &nonterminal : lowest_nonterminals) {
      compute_ancestor(nonterminal);
    }

    std::unordered_set<Operand> operands;
    operands.insert(core.variables.begin(), core.variables.end());
    operands.insert(core.locks.begin(), core.locks.end());

    for (const Operand &op : operands) {
      if (relv.lowest_occurences[op].size() == 1) {
        relv.lowest_common_ancestor[op] = *(relv.lowest_occurences[op].begin());

#ifdef DEBUG
        std::cout << "Lowest common ancestor of " << op << " = "
                  << relv.lowest_common_ancestor[op] << "\n\n";
#endif
      }
    }

    for (const auto &op : operands) {
      compute_lowest_common_ancestor(op);
    }

    // 3. Compute relevance
    for (const Nonterminal &nonterminal : core.topological_ordering) {
      if (lowest_nonterminals.contains(nonterminal)) {
        compute_relevance_base(nonterminal);
      } else {
        compute_relevance_inductive(nonterminal);
      }
    }
  }

  void topological_sort_dfs(Nonterminal vertex,
                            std::unordered_set<Nonterminal> &visited) {
    visited.insert(vertex);
    for (Symbol neighbor : core.grammar.rules.at(vertex)) {
      if (core.grammar.nonterminals.contains(neighbor)) {
        if (!visited.contains(neighbor)) {
          topological_sort_dfs(neighbor, visited);
        }
      }
    }
    core.topological_ordering.push_back(vertex);
  }

  void topological_sort() {
    Nonterminal start{"0"};
    std::unordered_set<Nonterminal> visited;
    topological_sort_dfs(start, visited);
  }

  void parse(std::string map_path, std::string grammar_path,
             std::string prep_path) {
    std::unique_ptr<Parser> parser{std::make_unique<Parser>()};
    parser->parse(map_path, grammar_path, prep_path);

    core.grammar = parser->grammar;
    core.threads = parser->threads;
    core.variables = parser->variables;
    core.locks = parser->locks;

    relv.maintain = parser->maintain;
  }

  void analyze() {
    topological_sort();

#ifdef PERF
    size_t th = 1;
#endif

    for (const Nonterminal &nonterminal : core.topological_ordering) {

#ifdef PERF
      std::cout << th++ << "/" << core.topological_ordering.size() << "\n";
      auto time_begin = std::chrono::high_resolution_clock::now();
#endif

      bool csv = analyze_csv(nonterminal);

#ifdef PERF
      auto time_end = std::chrono::high_resolution_clock::now();

      auto duration = std::chrono::duration_cast<std::chrono::seconds>(
                          time_end - time_begin)
                          .count();

      std::cout << "Time elapsed = " << duration << " sec\n";
#endif

      if (csv) {
        std::cout << "Conflict serializability violation detected in " +
                         nonterminal + "\n";
        return;
      }
    }

    std::cout << "No conflict serializability violation detected\n";
  }
};

#endif
