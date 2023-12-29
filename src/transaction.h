#ifndef TRANSACTION_H
#define TRANSACTION_H

#include "event.h"
#include "grammar.h"
#include <unordered_set>

using TransactionIdx = int;

struct Transaction {
  TransactionIdx idx;
  Thread thread;
  std::unordered_set<Event> content;
  std::unordered_set<Event> summary;
  std::unordered_set<Event> reversed_summary;

  Transaction() : idx{-1}, thread{}, content{}, summary{}, reversed_summary{} {}

  Transaction(TransactionIdx idx)
      : idx{idx}, thread{}, content{}, summary{}, reversed_summary{} {}

  bool operator==(const Transaction &other) const { return idx == other.idx; }
};

struct std::hash<Transaction> {
  std::size_t operator()(const Transaction &key) const {
    return std::hash<int>{}(key.idx);
  }
};

#endif
