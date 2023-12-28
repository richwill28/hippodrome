#ifndef GRAMMAR_H
#define GRAMMAR_H

#include "event.h"
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using Symbol = std::string;
using Nonterminal = std::string;
using Terminal = std::string;

struct Grammar {
  std::unordered_set<Nonterminal> nonterminals;
  std::unordered_set<Terminal> terminals;
  std::unordered_map<Nonterminal, std::vector<Symbol>> rules;
  std::unordered_map<Terminal, Event> content;
};

#endif
