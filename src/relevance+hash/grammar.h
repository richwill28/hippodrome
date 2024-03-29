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

bool is_terminal(const std::string &s) { return s.substr(0, 1) == "["; }

bool is_nonterminal(const std::string &s) { return !is_terminal(s); }

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
      if (is_terminal(rules.at(nonterminal).at(0))) {
        std::cout << content.at(rules.at(nonterminal).at(0)) << "\n";
        std::cout << content.at(rules.at(nonterminal).at(1)) << "\n";
      } else {
        generate(rules.at(nonterminal).at(0));
        generate(rules.at(nonterminal).at(1));
      }
    } else {
      for (const auto &term : rules.at(nonterminal)) {
        std::cout << content.at(term) << "\n";
      }
    }
    std::cout << "====================" + nonterminal +
                     "====================\n";
  }
};

#endif
