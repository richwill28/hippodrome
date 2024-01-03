#ifndef GRAMMAR_H
#define GRAMMAR_H

#include "event.h"
#include <iostream>
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

  void generate(Nonterminal nonterminal) const {
    if (rules.at(nonterminal).size() == 1) {
      std::cout << content.at(rules.at(nonterminal).at(0)) << "\n";
    } else if (rules.at(nonterminal).size() == 2) {
      generate(rules.at(nonterminal).at(0));
      generate(rules.at(nonterminal).at(1));
    } else {
      std::cerr << "Grammar is not in CNF\n";
      std::exit(EXIT_FAILURE);
    }
  }
};

#endif
