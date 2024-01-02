#ifndef TRANSACTION_H
#define TRANSACTION_H

#include "event.h"
#include "grammar.h"
#include <unordered_set>

using TransactionIdx = int;

struct Transaction {
  TransactionIdx idx;
  Thread thread;

  Transaction() : idx{-1}, thread{} {}

  Transaction(TransactionIdx idx) : idx{idx}, thread{} {}

  bool operator==(const Transaction &other) const { return idx == other.idx; }
};

template <> struct std::hash<Transaction> {
  std::size_t operator()(const Transaction &key) const {
    return std::hash<int>{}(key.idx);
  }
};

#endif
