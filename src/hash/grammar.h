#ifndef GRAMMAR_H
#define GRAMMAR_H

#include "event.h"
#include <ankerl/unordered_dense.h>
#include <iostream>
#include <string>
#include <vector>

using Symbol = std::string;
using Nonterminal = std::string;
using Terminal = std::string;

struct Grammar {
  ankerl::unordered_dense::set<Nonterminal> nonterminals;
  ankerl::unordered_dense::set<Terminal> terminals;
  ankerl::unordered_dense::map<Nonterminal, std::vector<Symbol>> rules;
  ankerl::unordered_dense::map<Terminal, Event> content;

  void generate(Nonterminal nonterminal) const {
    std::cout << "====================" + nonterminal +
                     "====================\n";
    if (rules.at(nonterminal).size() == 1) {
      std::cout << content.at(rules.at(nonterminal).at(0)) << "\n";
    } else if (rules.at(nonterminal).size() == 2) {
      generate(rules.at(nonterminal).at(0));
      generate(rules.at(nonterminal).at(1));
    } else {
      std::cerr << "Grammar is not in CNF\n";
      std::exit(EXIT_FAILURE);
    }
    std::cout << "====================" + nonterminal +
                     "====================\n";
  }
};

#endif
