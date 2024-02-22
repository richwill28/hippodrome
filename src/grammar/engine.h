#ifndef ENGINE_H
#define ENGINE_H

#include "event.h"
#include "grammar.h"
#include "graph.h"
#include "parser.h"
#include <memory>
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
  std::unique_ptr<Grammar> grammar;
  std::unique_ptr<std::vector<Nonterminal>> topological_ordering;

  std::unique_ptr<std::unordered_map<Nonterminal, Graph>> aux_graph;
  std::unique_ptr<std::unordered_map<Nonterminal, Graph>> cross_graph;
  std::unique_ptr<std::unordered_map<Nonterminal, bool>> csv;

  std::unique_ptr<std::unordered_set<Thread>> threads;
  std::unique_ptr<std::unordered_set<Operand>> variables;
  std::unique_ptr<std::unordered_set<Operand>> locks;

  TransactionIdx fresh_transaction_idx;

  // These are important for edges computation
  std::unique_ptr<std::unordered_map<std::pair<Nonterminal, Transaction>,
                                     std::unordered_set<Event>>>
      THS; // top half summary
  std::unique_ptr<std::unordered_map<std::pair<Nonterminal, Transaction>,
                                     std::unordered_set<Event>>>
      BHRS; // bottom half reversed summary

  // These are for memoization
  std::unique_ptr<std::unordered_map<std::pair<Nonterminal, Transaction>,
                                     std::unordered_set<Event>>>
      TUS; // trickle up summary
  std::unique_ptr<std::unordered_map<std::pair<Nonterminal, Transaction>,
                                     std::unordered_set<Event>>>
      TURS; // trickle up reversed summary

  Engine()
      : grammar{std::make_unique<Grammar>()},
        topological_ordering{std::make_unique<std::vector<Nonterminal>>()},
        aux_graph{std::make_unique<std::unordered_map<Nonterminal, Graph>>()},
        cross_graph{std::make_unique<std::unordered_map<Nonterminal, Graph>>()},
        csv{std::make_unique<std::unordered_map<Nonterminal, bool>>()},
        threads{std::make_unique<std::unordered_set<Thread>>()},
        variables{std::make_unique<std::unordered_set<Operand>>()},
        locks{std::make_unique<std::unordered_set<Operand>>()},
        fresh_transaction_idx{1},
        THS{std::make_unique<std::unordered_map<
            std::pair<Nonterminal, Transaction>, std::unordered_set<Event>>>()},
        BHRS{std::make_unique<std::unordered_map<
            std::pair<Nonterminal, Transaction>, std::unordered_set<Event>>>()},
        TUS{std::make_unique<std::unordered_map<
            std::pair<Nonterminal, Transaction>, std::unordered_set<Event>>>()},
        TURS{std::make_unique<
            std::unordered_map<std::pair<Nonterminal, Transaction>,
                               std::unordered_set<Event>>>()} {}

  Transaction make_transaction() {
    return Transaction{fresh_transaction_idx++};
  }

  void compute_aux_graph_one(Nonterminal A, Terminal a) {

#ifdef DEBUG
    std::cout << "Engine: compute_aux_graph (" << A << " -> " << a << ")\n";
#endif

    Event event = grammar->content[a];

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
      (*aux_graph)[A].vertices.insert(txn);
    }

    if (event.type != EventType::begin) {
      (*aux_graph)[A].reversed_vertices.insert(txn);
    }

    (*aux_graph)[A].first_transaction[std::make_tuple(
        event.thread, EventType::undefined, "")] = txn;
    (*aux_graph)[A].first_transaction[std::make_tuple(event.thread, event.type,
                                                      event.operand)] = txn;

    (*aux_graph)[A].last_transaction[std::make_tuple(
        event.thread, EventType::undefined, "")] = txn;
    (*aux_graph)[A].last_transaction[std::make_tuple(event.thread, event.type,
                                                     event.operand)] = txn;

    (*aux_graph)[A].content[txn].insert(event);

#ifdef DEBUG
    std::cout << (*aux_graph)[A] << "\n";
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
                     std::unordered_map<std::pair<Nonterminal, Transaction>,
                                        Transaction> &parent,
                     std::unordered_map<std::pair<Nonterminal, Transaction>,
                                        Transaction> &child) {

    std::unordered_map<Thread, Transaction> cross_transaction;

    for (const Transaction &vertex : (*aux_graph)[B].vertices) {
      if (vertex.idx == 0) {
        continue;
      }

      Transaction txn = make_transaction();
      txn.thread = vertex.thread;

      parent[std::make_pair(B, vertex)] = txn;
      child[std::make_pair(B, txn)] = vertex;

      (*aux_graph)[A].content[txn] = (*aux_graph)[B].content[vertex];

      cross_transaction[txn.thread] = txn;
    }

    for (const Transaction &reversed_vertex :
         (*aux_graph)[C].reversed_vertices) {

      if (cross_transaction.contains(reversed_vertex.thread)) {

        if (cross_transaction[reversed_vertex.thread].thread !=
            reversed_vertex.thread) {
          std::cerr << "merge_routine: something went wrong\n";
          std::exit(EXIT_FAILURE);
        }

        Transaction txn = cross_transaction[reversed_vertex.thread];

        parent[std::make_pair(C, reversed_vertex)] = txn;
        child[std::make_pair(C, txn)] = reversed_vertex;

        Annotation b_annotation = (*aux_graph)[B]
                                      .content[child[std::make_pair(B, txn)]]
                                      .begin()
                                      ->annotation;
        Annotation c_annotation = (*aux_graph)[C]
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

        (*aux_graph)[A].content[txn].clear();

        for (const Event &event :
             (*aux_graph)[B].content[child[std::make_pair(B, txn)]]) {
          (*aux_graph)[A].content[txn].insert(
              Event{event.thread, event.type, event.operand, a_annotation});
        }

        for (const Event &event :
             (*aux_graph)[C].content[child[std::make_pair(C, txn)]]) {
          (*aux_graph)[A].content[txn].insert(
              Event{event.thread, event.type, event.operand, a_annotation});
        }

      } else {

        Transaction txn = make_transaction();
        txn.thread = reversed_vertex.thread;

        parent[std::make_pair(C, reversed_vertex)] = txn;
        child[std::make_pair(C, txn)] = reversed_vertex;

        (*aux_graph)[A].content[txn] = (*aux_graph)[C].content[reversed_vertex];

        cross_transaction[txn.thread] = txn;
      }
    }
  }

  void compute_vertices(Nonterminal A, Nonterminal B, Nonterminal C,
                        std::unordered_map<std::pair<Nonterminal, Transaction>,
                                           Transaction> &parent,
                        std::unordered_map<std::pair<Nonterminal, Transaction>,
                                           Transaction> &child) {

    for (const Transaction &vertex : (*aux_graph)[C].vertices) {

      if (parent.contains(std::make_pair(C, vertex))) {
        (*aux_graph)[A].vertices.insert(parent[std::make_pair(C, vertex)]);

      } else {

        Transaction txn = make_transaction();
        txn.thread = vertex.thread;

        parent[std::make_pair(C, vertex)] = txn;
        child[std::make_pair(C, txn)] = vertex;

        (*aux_graph)[A].content[txn] = (*aux_graph)[C].content[vertex];

        (*aux_graph)[A].vertices.insert(txn);
      }
    }

    for (const Transaction &vertex : (*aux_graph)[B].vertices) {

      if (!parent.contains(std::make_pair(B, vertex))) {
        std::cerr << "compute_vertices: something went wrong\n";
        std::exit(EXIT_FAILURE);
      }

      if (!contains_event(
              (*aux_graph)[A].content[parent[std::make_pair(B, vertex)]],
              Event{"", EventType::end, "", Annotation::undefined})) {

        (*aux_graph)[A].vertices.insert(parent[std::make_pair(B, vertex)]);
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
         (*aux_graph)[B].reversed_vertices) {

      if (parent.contains(std::make_pair(B, reversed_vertex))) {
        (*aux_graph)[A].reversed_vertices.insert(
            parent[std::make_pair(B, reversed_vertex)]);

      } else {

        Transaction txn = make_transaction();
        txn.thread = reversed_vertex.thread;

        parent[std::make_pair(B, reversed_vertex)] = txn;
        child[std::make_pair(B, txn)] = reversed_vertex;

        (*aux_graph)[A].content[txn] = (*aux_graph)[B].content[reversed_vertex];

        (*aux_graph)[A].reversed_vertices.insert(txn);
      }
    }

    for (const Transaction &reversed_vertex :
         (*aux_graph)[C].reversed_vertices) {

      if (!parent.contains(std::make_pair(C, reversed_vertex))) {
        std::cerr << "compute_reversed_vertices: something went wrong\n";
        std::exit(EXIT_FAILURE);
      }

      if (!contains_event(
              (*aux_graph)[A]
                  .content[parent[std::make_pair(C, reversed_vertex)]],
              Event{"", EventType::begin, "", Annotation::undefined})) {

        (*aux_graph)[A].reversed_vertices.insert(
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

    for (const auto &[decor, ftxn] : (*aux_graph)[B].first_transaction) {

      if (parent.contains(std::make_pair(B, ftxn))) {
        (*aux_graph)[A].first_transaction[decor] =
            parent[std::make_pair(B, ftxn)];

      } else {

        Transaction txn = make_transaction();
        txn.thread = ftxn.thread;

        parent[std::make_pair(B, ftxn)] = txn;
        child[std::make_pair(B, txn)] = ftxn;

        (*aux_graph)[A].content[txn] = (*aux_graph)[B].content[ftxn];

        (*aux_graph)[A].first_transaction[decor] = txn;
      }
    }

    for (const auto &[decor, ftxn] : (*aux_graph)[C].first_transaction) {

      if ((*aux_graph)[A].first_transaction.contains(decor)) {
        continue;
      }

      if (parent.contains(std::make_pair(C, ftxn))) {
        (*aux_graph)[A].first_transaction[decor] =
            parent[std::make_pair(C, ftxn)];

      } else {

        Transaction txn = make_transaction();
        txn.thread = ftxn.thread;

        parent[std::make_pair(C, ftxn)] = txn;
        child[std::make_pair(C, txn)] = ftxn;

        (*aux_graph)[A].content[txn] = (*aux_graph)[C].content[ftxn];

        (*aux_graph)[A].first_transaction[decor] = txn;
      }
    }
  }

  void compute_last_transactions(
      Nonterminal A, Nonterminal B, Nonterminal C,
      std::unordered_map<std::pair<Nonterminal, Transaction>, Transaction>
          &parent,
      std::unordered_map<std::pair<Nonterminal, Transaction>, Transaction>
          &child) {

    for (const auto &[decor, ltxn] : (*aux_graph)[C].last_transaction) {

      if (parent.contains(std::make_pair(C, ltxn))) {
        (*aux_graph)[A].last_transaction[decor] =
            parent[std::make_pair(C, ltxn)];

      } else {

        Transaction txn = make_transaction();
        txn.thread = ltxn.thread;

        parent[std::make_pair(C, ltxn)] = txn;
        child[std::make_pair(C, txn)] = ltxn;

        (*aux_graph)[A].content[txn] = (*aux_graph)[C].content[ltxn];

        (*aux_graph)[A].last_transaction[decor] = txn;
      }
    }

    for (const auto &[decor, ltxn] : (*aux_graph)[B].last_transaction) {

      if ((*aux_graph)[A].last_transaction.contains(decor)) {
        continue;
      }

      if (parent.contains(std::make_pair(B, ltxn))) {
        (*aux_graph)[A].last_transaction[decor] =
            parent[std::make_pair(B, ltxn)];

      } else {

        Transaction txn = make_transaction();
        txn.thread = ltxn.thread;

        parent[std::make_pair(B, ltxn)] = txn;
        child[std::make_pair(B, txn)] = ltxn;

        (*aux_graph)[A].content[txn] = (*aux_graph)[B].content[ltxn];

        (*aux_graph)[A].last_transaction[decor] = txn;
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

    for (const auto &edge : (*aux_graph)[B].edges) {

      if (Ti != edge.first) {
        continue;
      }

      Transaction Tj = edge.second;

      if (!(*aux_graph)[B].vertices.contains(Tj)) {
        continue;
      }

      if (!(*aux_graph)[A].content.contains(parent.at(std::make_pair(B, Tj)))) {
        // Tj is not a cross-boundary transaction
        continue;
      }

      if (!contains_event(
              (*aux_graph)[A].content[parent.at(std::make_pair(B, Tj))],
              Event{"", EventType::end, "", Annotation::undefined})) {
        // Tj is an active transaction
        continue;
      }

      for (const Event &event : (*aux_graph)[B].content[Tj]) {

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

      res.insert((*aux_graph)[B].summary[Tj].begin(),
                 (*aux_graph)[B].summary[Tj].end());

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
          &parent) {

    std::unordered_set<Event> top_events;

    top_events.insert((*aux_graph)[B].content[Ti].begin(),
                      (*aux_graph)[B].content[Ti].end());
    top_events.insert((*aux_graph)[B].summary[Ti].begin(),
                      (*aux_graph)[B].summary[Ti].end());
    top_events.insert(neighbor_summary.begin(), neighbor_summary.end());

    std::vector<std::pair<Event, Event>> args;

    for (const Thread &ui : *threads) {
      args.push_back(std::make_pair(
          Event{ui, EventType::undefined, "", Annotation::undefined},
          Event{ui, EventType::undefined, "", Annotation::undefined}));
      args.push_back(std::make_pair(
          Event{"", EventType::fork, ui, Annotation::undefined},
          Event{ui, EventType::undefined, "", Annotation::undefined}));
      for (const Operand &v : *variables) {
        args.push_back(std::make_pair(
            Event{"", EventType::write, v, Annotation::undefined},
            Event{ui, EventType::read, v, Annotation::undefined}));
        args.push_back(std::make_pair(
            Event{"", EventType::write, v, Annotation::undefined},
            Event{ui, EventType::write, v, Annotation::undefined}));
        args.push_back(std::make_pair(
            Event{"", EventType::read, v, Annotation::undefined},
            Event{ui, EventType::write, v, Annotation::undefined}));
      }
      for (const Operand &l : *locks) {
        args.push_back(std::make_pair(
            Event{"", EventType::unlock, l, Annotation::undefined},
            Event{ui, EventType::lock, l, Annotation::undefined}));
      }
      for (const Thread &uj : *threads) {
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

      if (!(*aux_graph)[C].first_transaction.contains(decor)) {
        continue;
      }

      Transaction Tj = (*aux_graph)[C].first_transaction[decor];

      if (parent.contains(std::make_pair(B, Ti)) &&
          parent.contains(std::make_pair(C, Tj)) &&
          parent.at(std::make_pair(B, Ti)) ==
              parent.at(std::make_pair(C, Tj))) {
        // Ti and Tj are the same transaction
        continue;
      }

      if (!contains_event(
              (*aux_graph)[C].content[Tj],
              Event{"", EventType::end, "", Annotation::undefined})) {
        // Tj is an active transaction
        continue;
      }

      res.insert((*aux_graph)[C].content[Tj].begin(),
                 (*aux_graph)[C].content[Tj].end());
      res.insert((*aux_graph)[C].summary[Tj].begin(),
                 (*aux_graph)[C].summary[Tj].end());
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

        if (!(*aux_graph)[B].last_transaction.contains(decor)) {
          res.insert(
              Event{event.thread, event.type, event.operand, event.annotation});
          continue;
        }

        Transaction Tj = (*aux_graph)[B].last_transaction[decor];

        Annotation annotation = event.annotation;

        if (contains_event(
                (*aux_graph)[B].content[Tj],
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
        compute_trickle_down_summary(Tj, B, C, neighbor_summary, parent);

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

        if (!(*aux_graph)[B].last_transaction.contains(decor)) {
          continue;
        }

        Transaction Tk = (*aux_graph)[B].last_transaction[decor];

        std::unordered_set<Event> res = compute_trickle_up_summary_fixpoint(
            Ti, Tk, A, B, C, parent, child, avoid);

        trickle_up_summary.insert(res.begin(), res.end());
      }
    }

    std::unordered_set<Event> res;

    res.insert((*aux_graph)[B].content[Tj].begin(),
               (*aux_graph)[B].content[Tj].end());
    res.insert((*aux_graph)[B].summary[Tj].begin(),
               (*aux_graph)[B].summary[Tj].end());
    res.insert(neighbor_summary.begin(), neighbor_summary.end());
    res.insert(annotated_trickle_down_summary.begin(),
               annotated_trickle_down_summary.end());
    res.insert(trickle_up_summary.begin(), trickle_up_summary.end());

    (*THS)[std::make_pair(A, Ti)].insert((*aux_graph)[B].content[Tj].begin(),
                                         (*aux_graph)[B].content[Tj].end());
    (*THS)[std::make_pair(A, Ti)].insert((*aux_graph)[B].summary[Tj].begin(),
                                         (*aux_graph)[B].summary[Tj].end());
    (*THS)[std::make_pair(A, Ti)].insert(neighbor_summary.begin(),
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

    if (TUS->contains(decor)) {
      return (*TUS)[decor];
    }

    std::unordered_set<Thread> avoid;

    (*TUS)[decor] = compute_trickle_up_summary_fixpoint(Ti, Tj, A, B, C, parent,
                                                        child, avoid);

    return (*TUS)[decor];
  }

  void compute_summary(
      Transaction Ti, Nonterminal A, Nonterminal B, Nonterminal C,
      const std::unordered_map<std::pair<Nonterminal, Transaction>, Transaction>
          &parent,
      const std::unordered_map<std::pair<Nonterminal, Transaction>, Transaction>
          &child) {

    if ((*aux_graph)[A].summary.contains(Ti)) {
      return;
    }

#ifdef DEBUG
    std::cout << "Engine: compute_summary(Ti = " << Ti << ", A = " << A
              << ", B = " << B << ", C = " << C << ")\n";
#endif

    std::unordered_set<Event> b_summary;

    if (child.contains(std::make_pair(B, Ti))) {
      b_summary = (*aux_graph)[B].summary[child.at(std::make_pair(B, Ti))];
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
          child.at(std::make_pair(B, Ti)), B, C, neighbor_summary, parent);
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
      c_summary = (*aux_graph)[C].summary[child.at(std::make_pair(C, Ti))];
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

        if (!(*aux_graph)[B].last_transaction.contains(decor)) {
          continue;
        }

        Transaction Tj = (*aux_graph)[B].last_transaction[decor];

        std::unordered_set<Event> res =
            compute_trickle_up_summary(Ti, Tj, A, B, C, parent, child);

        trickle_up_summary.insert(res.begin(), res.end());
      }
    }

    (*aux_graph)[A].summary[Ti].insert(b_summary.begin(), b_summary.end());
    (*aux_graph)[A].summary[Ti].insert(neighbor_summary.begin(),
                                       neighbor_summary.end());
    (*aux_graph)[A].summary[Ti].insert(annotated_bottom_summary.begin(),
                                       annotated_bottom_summary.end());
    (*aux_graph)[A].summary[Ti].insert(trickle_up_summary.begin(),
                                       trickle_up_summary.end());

    (*THS)[std::make_pair(A, Ti)].insert(b_summary.begin(), b_summary.end());
    (*THS)[std::make_pair(A, Ti)].insert(neighbor_summary.begin(),
                                         neighbor_summary.end());
  }

  void compute_summaries(
      Nonterminal A, Nonterminal B, Nonterminal C,
      const std::unordered_map<std::pair<Nonterminal, Transaction>, Transaction>
          &parent,
      const std::unordered_map<std::pair<Nonterminal, Transaction>, Transaction>
          &child) {

    for (const Transaction &Ti : (*aux_graph)[A].vertices) {
      compute_summary(Ti, A, B, C, parent, child);
    }

    for (const auto &[_, Ti] : (*aux_graph)[A].first_transaction) {
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

    for (const auto &edge : (*aux_graph)[C].edges) {

      if (Ti != edge.second) {
        continue;
      }

      Transaction Tj = edge.first;

      if (!(*aux_graph)[C].reversed_vertices.contains(Tj)) {
        continue;
      }

      if (!(*aux_graph)[A].content.contains(parent.at(std::make_pair(C, Tj)))) {
        // Tj is not a cross-boundary transaction
        continue;
      }

      if (!contains_event(
              (*aux_graph)[A].content[parent.at(std::make_pair(C, Tj))],
              Event{"", EventType::begin, "", Annotation::undefined})) {
        // Tj is a reversed active transaction
        continue;
      }

      for (const Event &event : (*aux_graph)[C].content[Tj]) {

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

      res.insert((*aux_graph)[C].reversed_summary[Tj].begin(),
                 (*aux_graph)[C].reversed_summary[Tj].end());

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
          &parent) {

    std::unordered_set<Event> bottom_events;

    bottom_events.insert((*aux_graph)[C].content[Ti].begin(),
                         (*aux_graph)[C].content[Ti].end());
    bottom_events.insert((*aux_graph)[C].summary[Ti].begin(),
                         (*aux_graph)[C].summary[Ti].end());
    bottom_events.insert(neighbor_reversed_summary.begin(),
                         neighbor_reversed_summary.end());

    std::vector<std::pair<Event, Event>> args;

    for (const Thread &ui : *threads) {
      args.push_back(std::make_pair(
          Event{ui, EventType::undefined, "", Annotation::undefined},
          Event{ui, EventType::undefined, "", Annotation::undefined}));
      args.push_back(std::make_pair(
          Event{ui, EventType::undefined, "", Annotation::undefined},
          Event{"", EventType::join, ui, Annotation::undefined}));
      for (const Operand &v : *variables) {
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
      for (const Operand &l : *locks) {
        args.push_back(std::make_pair(
            Event{ui, EventType::unlock, l, Annotation::undefined},
            Event{"", EventType::lock, l, Annotation::undefined}));
      }
      for (const Thread &uj : *threads) {
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

      if (!(*aux_graph)[B].last_transaction.contains(decor)) {
        continue;
      }

      Transaction Tj = (*aux_graph)[B].last_transaction[decor];

      if (parent.contains(std::make_pair(C, Ti)) &&
          parent.contains(std::make_pair(B, Tj)) &&
          parent.at(std::make_pair(C, Ti)) ==
              parent.at(std::make_pair(B, Tj))) {
        // Ti and Tj are the same transaction
        continue;
      }

      if (!contains_event(
              (*aux_graph)[B].content[Tj],
              Event{"", EventType::begin, "", Annotation::undefined})) {
        // Tj is a reversed active transaction
        continue;
      }

      res.insert((*aux_graph)[B].content[Tj].begin(),
                 (*aux_graph)[B].content[Tj].end());
      res.insert((*aux_graph)[B].reversed_summary[Tj].begin(),
                 (*aux_graph)[B].reversed_summary[Tj].end());
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

        if (!(*aux_graph)[C].first_transaction.contains(decor)) {
          res.insert(
              Event{event.thread, event.type, event.operand, event.annotation});
          continue;
        }

        Transaction Tj = (*aux_graph)[C].first_transaction[decor];

        Annotation annotation = event.annotation;

        if (contains_event(
                (*aux_graph)[C].content[Tj],
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
            Tj, B, C, neighbor_reversed_summary, parent);

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

        if (!(*aux_graph)[C].first_transaction.contains(decor)) {
          continue;
        }

        Transaction Tk = (*aux_graph)[C].first_transaction[decor];

        std::unordered_set<Event> res =
            compute_trickle_up_reversed_summary_fixpoint(Ti, Tk, A, B, C,
                                                         parent, child, avoid);

        trickle_up_reversed_summary.insert(res.begin(), res.end());
      }
    }

    std::unordered_set<Event> res;

    res.insert((*aux_graph)[C].content[Tj].begin(),
               (*aux_graph)[C].content[Tj].end());
    res.insert((*aux_graph)[C].reversed_summary[Tj].begin(),
               (*aux_graph)[C].reversed_summary[Tj].end());
    res.insert(neighbor_reversed_summary.begin(),
               neighbor_reversed_summary.end());
    res.insert(annotated_trickle_down_reversed_summary.begin(),
               annotated_trickle_down_reversed_summary.end());
    res.insert(trickle_up_reversed_summary.begin(),
               trickle_up_reversed_summary.end());

    (*BHRS)[std::make_pair(A, Ti)].insert((*aux_graph)[C].content[Tj].begin(),
                                          (*aux_graph)[C].content[Tj].end());
    (*BHRS)[std::make_pair(A, Ti)].insert(
        (*aux_graph)[C].reversed_summary[Tj].begin(),
        (*aux_graph)[C].reversed_summary[Tj].end());
    (*BHRS)[std::make_pair(A, Ti)].insert(neighbor_reversed_summary.begin(),
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

    if (TURS->contains(decor)) {
      return (*TURS)[decor];
    }

    std::unordered_set<Thread> avoid;

    (*TURS)[decor] = compute_trickle_up_reversed_summary_fixpoint(
        Ti, Tj, A, B, C, parent, child, avoid);

    return (*TURS)[decor];
  }

  void compute_reversed_summary(
      Transaction Ti, Nonterminal A, Nonterminal B, Nonterminal C,
      const std::unordered_map<std::pair<Nonterminal, Transaction>, Transaction>
          &parent,
      const std::unordered_map<std::pair<Nonterminal, Transaction>, Transaction>
          &child) {

    if ((*aux_graph)[A].reversed_summary.contains(Ti)) {
      return;
    }

    std::unordered_set<Event> c_reversed_summary;

    if (child.contains(std::make_pair(C, Ti))) {
      c_reversed_summary =
          (*aux_graph)[C].reversed_summary[child.at(std::make_pair(C, Ti))];
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
          parent);
    }

    std::unordered_set<Event> b_reversed_summary;

    if (child.contains(std::make_pair(B, Ti))) {
      b_reversed_summary =
          (*aux_graph)[B].reversed_summary[child.at(std::make_pair(B, Ti))];
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

        if (!(*aux_graph)[C].first_transaction.contains(decor)) {
          continue;
        }

        Transaction Tj = (*aux_graph)[C].first_transaction[decor];

        std::unordered_set<Event> res =
            compute_trickle_up_reversed_summary(Ti, Tj, A, B, C, parent, child);

        trickle_up_reversed_summary.insert(res.begin(), res.end());
      }
    }

    (*aux_graph)[A].reversed_summary[Ti].insert(c_reversed_summary.begin(),
                                                c_reversed_summary.end());
    (*aux_graph)[A].reversed_summary[Ti].insert(
        neighbor_reversed_summary.begin(), neighbor_reversed_summary.end());
    (*aux_graph)[A].reversed_summary[Ti].insert(top_reversed_summary.begin(),
                                                top_reversed_summary.end());
    (*aux_graph)[A].reversed_summary[Ti].insert(
        trickle_up_reversed_summary.begin(), trickle_up_reversed_summary.end());

    (*BHRS)[std::make_pair(A, Ti)].insert(c_reversed_summary.begin(),
                                          c_reversed_summary.end());
    (*BHRS)[std::make_pair(A, Ti)].insert(neighbor_reversed_summary.begin(),
                                          neighbor_reversed_summary.end());
  }

  void compute_reversed_summaries(
      Nonterminal A, Nonterminal B, Nonterminal C,
      const std::unordered_map<std::pair<Nonterminal, Transaction>, Transaction>
          &parent,
      const std::unordered_map<std::pair<Nonterminal, Transaction>, Transaction>
          &child) {

    for (const Transaction &Ti : (*aux_graph)[A].reversed_vertices) {
      compute_reversed_summary(Ti, A, B, C, parent, child);
    }

    for (const auto &[_, Ti] : (*aux_graph)[A].last_transaction) {
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
        (*aux_graph)[C].edges.contains(
            std::make_pair(child.at(std::make_pair(C, Ti)),
                           child.at(std::make_pair(C, Tj))))) {
      (*aux_graph)[A].edges.insert(std::make_pair(Ti, Tj));
      return;
    }

    if (child.contains(std::make_pair(B, Ti)) &&
        child.contains(std::make_pair(B, Tj)) &&
        (*aux_graph)[B].edges.contains(
            std::make_pair(child.at(std::make_pair(B, Ti)),
                           child.at(std::make_pair(B, Tj))))) {
      (*aux_graph)[A].edges.insert(std::make_pair(Ti, Tj));
      return;
    }

    if (Ti != Tj && child.contains(std::make_pair(B, Ti)) &&
        child.contains(std::make_pair(C, Tj))) {
      for (const Event &ei :
           (*aux_graph)[B].content[child.at(std::make_pair(B, Ti))]) {
        for (const Event &ej :
             (*aux_graph)[C].content[child.at(std::make_pair(C, Tj))]) {
          if (ei.conflict(ej)) {
            (*aux_graph)[A].edges.insert(std::make_pair(Ti, Tj));
            return;
          }
        }
      }
    }

    if (child.contains(std::make_pair(B, Ti)) &&
        child.contains(std::make_pair(C, Tj))) {
      for (const auto &[_, Tk] : (*aux_graph)[C].first_transaction) {
        if (parent.contains(std::make_pair(C, Tk)) &&
            Ti != parent.at(std::make_pair(C, Tk)) &&
            contains_event(
                (*aux_graph)[C].content[Tk],
                Event{"", EventType::end, "", Annotation::undefined})) {
          for (const Event &ei :
               (*aux_graph)[B].content[child.at(std::make_pair(B, Ti))]) {
            for (const Event &ek : (*aux_graph)[C].content[Tk]) {
              if (ei.conflict(ek) && (Tk.thread == Tj.thread ||
                                      (*aux_graph)[C].reachable(
                                          Tk, child.at(std::make_pair(C, Tj)),
                                          (*aux_graph)[C].vertices))) {
                (*aux_graph)[A].edges.insert(std::make_pair(Ti, Tj));
                return;
              }
            }
          }
        }
      }
    }

    if (child.contains(std::make_pair(C, Tj))) {
      for (const Event &ei : (*THS)[std::make_pair(A, Ti)]) {
        for (const Event &ej :
             (*aux_graph)[C].content[child.at(std::make_pair(C, Tj))]) {
          if (ei.conflict(ej)) {
            (*aux_graph)[A].edges.insert(std::make_pair(Ti, Tj));
            return;
          }
        }
      }
    }

    if (child.contains(std::make_pair(C, Tj))) {
      for (const auto &[_, Tk] : (*aux_graph)[C].first_transaction) {
        if (contains_event(
                (*aux_graph)[C].content[Tk],
                Event{"", EventType::end, "", Annotation::undefined})) {
          for (const Event &ei : (*THS)[std::make_pair(A, Ti)]) {
            for (const Event &ek : (*aux_graph)[C].content[Tk]) {
              if (ei.conflict(ek) && (Tk.thread == Tj.thread ||
                                      (*aux_graph)[C].reachable(
                                          Tk, child.at(std::make_pair(C, Tj)),
                                          (*aux_graph)[C].vertices))) {
                (*aux_graph)[A].edges.insert(std::make_pair(Ti, Tj));
                return;
              }
            }
          }
        }
      }
    }

    if (child.contains(std::make_pair(B, Tj))) {
      for (const Event &ei : (*THS)[std::make_pair(A, Ti)]) {
        Transaction Tk = (*aux_graph)[B].last_transaction[std::make_tuple(
            ei.thread, EventType::undefined, "")];

        if (Tk.thread == Tj.thread) {
          (*aux_graph)[A].edges.insert(std::make_pair(Ti, Tj));
          return;
        }

        if (parent.contains(std::make_pair(B, Tk)) &&
            (*aux_graph)[A].content.contains(
                parent.at(std::make_pair(B, Tk))) &&
            contains_event(
                (*aux_graph)[A].content[parent.at(std::make_pair(B, Tk))],
                Event{"", EventType::end, "", Annotation::undefined})) {
          if ((*aux_graph)[B].edges.contains(
                  std::make_pair(Tk, child.at(std::make_pair(B, Tj))))) {
            (*aux_graph)[A].edges.insert(std::make_pair(Ti, Tj));
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

    for (const Transaction &Ti : (*aux_graph)[A].vertices) {
      for (const Transaction &Tj : (*aux_graph)[A].vertices) {
        compute_edge(Ti, Tj, A, B, C, parent, child);
      }
    }

    for (const auto &[_, Ti] : (*aux_graph)[A].first_transaction) {
      if ((*aux_graph)[A].vertices.contains(Ti)) {
        continue;
      }
      for (const Transaction &Tj : (*aux_graph)[A].vertices) {
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
        (*aux_graph)[B].edges.contains(
            std::make_pair(child.at(std::make_pair(B, Tj)),
                           child.at(std::make_pair(B, Ti))))) {
      (*aux_graph)[A].edges.insert(std::make_pair(Tj, Ti));
      return;
    }

    if (child.contains(std::make_pair(C, Tj)) &&
        child.contains(std::make_pair(C, Ti)) &&
        (*aux_graph)[C].edges.contains(
            std::make_pair(child.at(std::make_pair(C, Tj)),
                           child.at(std::make_pair(C, Ti))))) {
      (*aux_graph)[A].edges.insert(std::make_pair(Tj, Ti));
      return;
    }

    if (Tj != Ti && child.contains(std::make_pair(B, Tj)) &&
        child.contains(std::make_pair(C, Ti))) {
      for (const Event &ej :
           (*aux_graph)[B].content[child.at(std::make_pair(B, Tj))]) {
        for (const Event &ei :
             (*aux_graph)[C].content[child.at(std::make_pair(C, Ti))]) {
          if (ej.conflict(ei)) {
            (*aux_graph)[A].edges.insert(std::make_pair(Tj, Ti));
            return;
          }
        }
      }
    }

    if (child.contains(std::make_pair(C, Ti)) &&
        child.contains(std::make_pair(B, Tj))) {
      for (const auto &[_, Tk] : (*aux_graph)[B].last_transaction) {
        if (parent.contains(std::make_pair(B, Tk)) &&
            Ti != parent.at(std::make_pair(B, Tk)) &&
            contains_event(
                (*aux_graph)[B].content[Tk],
                Event{"", EventType::begin, "", Annotation::undefined})) {
          for (const Event &ek : (*aux_graph)[B].content[Tk]) {
            for (const Event &ei :
                 (*aux_graph)[C].content[child.at(std::make_pair(C, Ti))]) {
              if (ek.conflict(ei) && (Tk.thread == Tj.thread ||
                                      (*aux_graph)[B].reachable(
                                          child.at(std::make_pair(B, Tj)), Tk,
                                          (*aux_graph)[B].reversed_vertices))) {
                (*aux_graph)[A].edges.insert(std::make_pair(Tj, Ti));
                return;
              }
            }
          }
        }
      }
    }

    if (child.contains(std::make_pair(B, Tj))) {
      for (const Event &ej :
           (*aux_graph)[B].content[child.at(std::make_pair(B, Tj))]) {
        for (const Event &ei : (*BHRS)[std::make_pair(A, Ti)]) {
          if (ej.conflict(ei)) {
            (*aux_graph)[A].edges.insert(std::make_pair(Tj, Ti));
            return;
          }
        }
      }
    }

    if (child.contains(std::make_pair(B, Tj))) {
      for (const auto &[_, Tk] : (*aux_graph)[B].last_transaction) {
        if (contains_event(
                (*aux_graph)[B].content[Tk],
                Event{"", EventType::begin, "", Annotation::undefined})) {
          for (const Event &ei : (*BHRS)[std::make_pair(A, Ti)]) {
            for (const Event &ek : (*aux_graph)[B].content[Tk]) {
              if (ek.conflict(ei) && (Tk.thread == Tj.thread ||
                                      (*aux_graph)[B].reachable(
                                          child.at(std::make_pair(B, Tj)), Tk,
                                          (*aux_graph)[B].reversed_vertices))) {
                (*aux_graph)[A].edges.insert(std::make_pair(Tj, Ti));
                return;
              }
            }
          }
        }
      }
    }

    if (child.contains(std::make_pair(C, Tj))) {
      for (const Event &ei : (*BHRS)[std::make_pair(A, Ti)]) {
        Transaction Tk = (*aux_graph)[C].first_transaction[std::make_tuple(
            ei.thread, EventType::undefined, "")];

        if (Tk.thread == Tj.thread) {
          (*aux_graph)[A].edges.insert(std::make_pair(Tj, Ti));
          return;
        }

        if (parent.contains(std::make_pair(C, Tk)) &&
            (*aux_graph)[A].content.contains(
                parent.at(std::make_pair(C, Tk))) &&
            contains_event(
                (*aux_graph)[A].content[parent.at(std::make_pair(C, Tk))],
                Event{"", EventType::begin, "", Annotation::undefined})) {
          if ((*aux_graph)[C].edges.contains(
                  std::make_pair(child.at(std::make_pair(C, Tj)), Tk))) {
            (*aux_graph)[A].edges.insert(std::make_pair(Tj, Ti));
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

    for (const Transaction &Ti : (*aux_graph)[A].reversed_vertices) {
      for (const Transaction &Tj : (*aux_graph)[A].reversed_vertices) {
        compute_reversed_edge(Tj, Ti, A, B, C, parent, child);
      }
    }

    for (const auto &[_, Ti] : (*aux_graph)[A].last_transaction) {
      if ((*aux_graph)[A].reversed_vertices.contains(Ti)) {
        continue;
      }
      for (const Transaction &Tj : (*aux_graph)[A].reversed_vertices) {
        compute_reversed_edge(Tj, Ti, A, B, C, parent, child);
      }
    }
  }

  void refresh_aux_graph(Nonterminal B, Nonterminal C) {

#ifdef DEBUG
    std::cout << "Engine: refresh_graph (" << B << ", " << C << ")\n";
#endif

    std::unordered_map<Transaction, Transaction> fresh;

    for (const auto &txn : (*aux_graph)[B].vertices) {
      if (!fresh.contains(txn)) {
        fresh[txn] = make_transaction();
        fresh[txn].thread = txn.thread;
      }
      (*aux_graph)[C].vertices.insert(fresh[txn]);
    }

    for (const auto &txn : (*aux_graph)[B].reversed_vertices) {
      if (!fresh.contains(txn)) {
        fresh[txn] = make_transaction();
        fresh[txn].thread = txn.thread;
      }
      (*aux_graph)[C].reversed_vertices.insert(fresh[txn]);
    }

    for (const auto &[decor, txn] : (*aux_graph)[B].first_transaction) {
      if (!fresh.contains(txn)) {
        fresh[txn] = make_transaction();
        fresh[txn].thread = txn.thread;
      }
      (*aux_graph)[C].first_transaction[decor] = fresh[txn];
    }

    for (const auto &[decor, txn] : (*aux_graph)[B].last_transaction) {
      if (!fresh.contains(txn)) {
        fresh[txn] = make_transaction();
        fresh[txn].thread = txn.thread;
      }
      (*aux_graph)[C].last_transaction[decor] = fresh[txn];
    }

    for (const auto &[Ti, Tj] : (*aux_graph)[B].edges) {
      (*aux_graph)[C].edges.insert(std::make_pair(fresh.at(Ti), fresh.at(Tj)));
    }

    for (const auto &[txn, _] : (*aux_graph)[B].content) {
      if (!fresh.contains(txn)) {
        fresh[txn] = make_transaction();
        fresh[txn].thread = txn.thread;
      }
      (*aux_graph)[C].content[fresh.at(txn)] = (*aux_graph)[B].content[txn];
    }

    for (const auto &[txn, _] : (*aux_graph)[B].summary) {
      if (!fresh.contains(txn)) {
        fresh[txn] = make_transaction();
        fresh[txn].thread = txn.thread;
      }
      (*aux_graph)[C].summary[fresh.at(txn)] = (*aux_graph)[B].summary[txn];
    }

    for (const auto &[txn, _] : (*aux_graph)[B].reversed_summary) {
      if (!fresh.contains(txn)) {
        fresh[txn] = make_transaction();
        fresh[txn].thread = txn.thread;
      }
      (*aux_graph)[C].reversed_summary[fresh.at(txn)] =
          (*aux_graph)[B].reversed_summary[txn];
    }
  }

  void compute_aux_graph_two(Nonterminal A, Nonterminal B, Nonterminal C) {

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
    std::cout << (*aux_graph)[A] << "\n";
#endif

    merge_routine(A, B, C, parent, child);

#ifdef DEBUG
    std::cout << "After merge_routine\n";
    std::cout << (*aux_graph)[A] << "\n";
#endif

    compute_vertices(A, B, C, parent, child);

#ifdef DEBUG
    std::cout << "After compute_vertices\n";
    std::cout << (*aux_graph)[A] << "\n";
#endif

    compute_reversed_vertices(A, B, C, parent, child);

#ifdef DEBUG
    std::cout << "After compute_reversed_vertices\n";
    std::cout << (*aux_graph)[A] << "\n";
#endif

    compute_first_transactions(A, B, C, parent, child);

#ifdef DEBUG
    std::cout << "After compute_first_transactions\n";
    std::cout << (*aux_graph)[A] << "\n";
#endif

    compute_last_transactions(A, B, C, parent, child);

#ifdef DEBUG
    std::cout << "After compute_last_transactions\n";
    std::cout << (*aux_graph)[A] << "\n";
#endif

    compute_summaries(A, B, C, parent, child);

#ifdef DEBUG
    std::cout << "After compute_summaries\n";
    std::cout << (*aux_graph)[A] << "\n";
#endif

    compute_reversed_summaries(A, B, C, parent, child);

#ifdef DEBUG
    std::cout << "After compute_reversed_summaries\n";
    std::cout << (*aux_graph)[A] << "\n";
#endif

    compute_edges(A, B, C, parent, child);

#ifdef DEBUG
    std::cout << "After compute_edges\n";
    std::cout << (*aux_graph)[A] << "\n";
#endif

    compute_reversed_edges(A, B, C, parent, child);

#ifdef DEBUG
    std::cout << "After compute_reversed_edges\n";
    std::cout << (*aux_graph)[A] << "\n";
#endif
  }

  void compute_aux_graph(Nonterminal nonterminal) {
    if (aux_graph->contains(nonterminal)) {
      return;
    }

    std::vector<Nonterminal> chunks = grammar->rules[nonterminal];

    if (chunks.size() == 1) {
      compute_aux_graph_one(nonterminal, chunks[0]);
    } else if (chunks.size() == 2) {
      compute_aux_graph_two(nonterminal, chunks[0], chunks[1]);
    } else {
      std::cerr << "compute_aux_graph: something went wrong\n";
      std::exit(EXIT_FAILURE);
    }
  }

  void compute_cross_graph(Nonterminal A) {
    if (cross_graph->contains(A)) {
      return;
    }

    std::vector<Nonterminal> chunks = grammar->rules[A];
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

    for (const Transaction &vertex : (*aux_graph)[B].vertices) {

      cross_transaction[vertex.thread] = make_transaction();
      cross_transaction[vertex.thread].thread = vertex.thread;

      content[std::make_pair(B, cross_transaction[vertex.thread])] =
          (*aux_graph)[B].content[vertex];
      summary[std::make_pair(B, cross_transaction[vertex.thread])] =
          (*aux_graph)[B].summary[vertex];
    }

    for (const Transaction &reversed_vertex :
         (*aux_graph)[C].reversed_vertices) {

      if (cross_transaction.contains(reversed_vertex.thread)) {

        if (cross_transaction[reversed_vertex.thread].thread !=
            reversed_vertex.thread) {
          std::cerr << "compute_aux_graph: something went wrong\n";
          std::exit(EXIT_FAILURE);
        }

        content[std::make_pair(C, cross_transaction[reversed_vertex.thread])] =
            (*aux_graph)[C].content[reversed_vertex];
        reversed_summary[std::make_pair(
            C, cross_transaction[reversed_vertex.thread])] =
            (*aux_graph)[C].reversed_summary[reversed_vertex];

      } else {

        cross_transaction[reversed_vertex.thread] = make_transaction();
        cross_transaction[reversed_vertex.thread].thread =
            reversed_vertex.thread;

        content[std::make_pair(C, cross_transaction[reversed_vertex.thread])] =
            (*aux_graph)[C].content[reversed_vertex];
        reversed_summary[std::make_pair(
            C, cross_transaction[reversed_vertex.thread])] =
            (*aux_graph)[C].reversed_summary[reversed_vertex];
      }
    }

    for (const std::pair<const Thread, Transaction> &it : cross_transaction) {
      (*cross_graph)[A].vertices.insert(it.second);
    }

    for (const std::pair<Transaction, Transaction> &edge :
         (*aux_graph)[B].edges) {
      if ((*aux_graph)[B].vertices.contains(edge.first) &&
          (*aux_graph)[B].vertices.contains(edge.second)) {

        (*cross_graph)[A].edges.insert(
            std::make_pair(cross_transaction[edge.first.thread],
                           cross_transaction[edge.second.thread]));
      }
    }

    for (const std::pair<Transaction, Transaction> &edge :
         (*aux_graph)[C].edges) {
      if ((*aux_graph)[C].reversed_vertices.contains(edge.first) &&
          (*aux_graph)[C].reversed_vertices.contains(edge.second)) {

        (*cross_graph)[A].edges.insert(
            std::make_pair(cross_transaction[edge.first.thread],
                           cross_transaction[edge.second.thread]));
      }
    }

    for (const Transaction &v : (*cross_graph)[A].vertices) {
      for (const Transaction &w : (*cross_graph)[A].vertices) {

        if (v != w) {
          for (const Event &e : content[std::make_pair(B, v)]) {
            for (const Event &f : content[std::make_pair(C, w)]) {
              if (e.conflict(f)) {
                (*cross_graph)[A].edges.insert(std::make_pair(v, w));
              }
            }
          }
        }

        for (const Event &e : content[std::make_pair(B, v)]) {
          for (const Event &f : reversed_summary[std::make_pair(C, w)]) {
            if (e.conflict(f)) {
              (*cross_graph)[A].edges.insert(std::make_pair(v, w));
            }
          }
        }

        for (const Event &e : summary[std::make_pair(B, v)]) {
          for (const Event &f : content[std::make_pair(C, w)]) {
            if (e.conflict(f)) {
              (*cross_graph)[A].edges.insert(std::make_pair(v, w));
            }
          }
        }

        for (const Event &e : summary[std::make_pair(B, v)]) {
          for (const Event &f : reversed_summary[std::make_pair(C, w)]) {
            if (e.conflict(f)) {
              (*cross_graph)[A].edges.insert(std::make_pair(v, w));
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

    std::vector<Symbol> chunks = grammar->rules[nonterminal];

    if (chunks.size() == 1) {
      (*csv)[nonterminal] = false;
      compute_aux_graph(nonterminal);
      return (*csv)[nonterminal];

    } else if (chunks.size() == 2) {
      compute_cross_graph(nonterminal);

#ifdef DEBUG
      std::cout << "Cross graph:\n";
      std::cout << cross_graph[nonterminal] << "\n";
#endif

      bool violation = (*cross_graph)[nonterminal].cyclic();
      (*csv)[nonterminal] = violation;
      if (!violation) {
        compute_aux_graph(nonterminal);
      }
      return violation;
    }

    std::cerr << "Engine: Grammar is not in CNF\n";
    std::exit(EXIT_FAILURE);
  }

  void topological_sort_dfs(Nonterminal &vertex,
                            std::unordered_set<Nonterminal> &visited) {
    visited.insert(vertex);
    for (Symbol neighbor : grammar->rules.at(vertex)) {
      if (grammar->nonterminals.contains(neighbor)) {
        if (!visited.contains(neighbor)) {
          topological_sort_dfs(neighbor, visited);
        }
      }
    }
    topological_ordering->push_back(vertex);
  }

  void topological_sort() {
    Nonterminal start{"0"};
    std::unordered_set<Nonterminal> visited;
    topological_sort_dfs(start, visited);
  }

  void analyze(std::string map_path, std::string grammar_path) {
    Parser parser{};
    parser.parse(map_path, grammar_path);

    grammar = std::move(parser.grammar);
    threads = std::move(parser.threads);
    variables = std::move(parser.variables);
    locks = std::move(parser.locks);

    topological_sort();

    for (Nonterminal nonterminal : *topological_ordering) {
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
