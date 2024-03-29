#ifndef TRANSACTION_H
#define TRANSACTION_H

#include "event.h"
#include "grammar.h"
#include <ankerl/unordered_dense.h>
#include <string>

using TransactionIdx = uint64_t;

struct Transaction {
  TransactionIdx idx;
  Thread thread;

  Transaction() : idx{0}, thread{} {}

  Transaction(TransactionIdx index) : idx{index}, thread{} {}

  std::string to_string() const { return "T" + std::to_string(idx); }

  bool operator==(const Transaction &other) const { return idx == other.idx; }
};

template <> struct std::hash<Transaction> {
  std::size_t operator()(const Transaction &key) const {
    return std::hash<TransactionIdx>{}(key.idx);
  }
};

std::ostream &operator<<(std::ostream &os, const Transaction &transaction) {
  os << transaction.to_string();
  return os;
}

#endif
