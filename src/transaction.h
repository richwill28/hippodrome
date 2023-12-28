#ifndef TRANSACTION_H
#define TRANSACTION_H

#include "event.h"
#include "grammar.h"
#include <unordered_set>

struct Transaction {
  int idx;
  Thread thread;
  std::unordered_map<Nonterminal, std::unordered_set<Event>> content;
  std::unordered_map<Nonterminal, std::unordered_set<Event>> summary;
};

#endif
