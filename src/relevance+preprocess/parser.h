#ifndef PARSER_H
#define PARSER_H

#include "event.h"
#include "grammar.h"
#include "util.h"
#include <fstream>
#include <iostream>
#include <regex>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct Parser {
  Grammar grammar;
  std::unordered_set<Thread> threads;
  std::unordered_set<Operand> variables;
  std::unordered_set<Operand> locks;
  std::unordered_map<Nonterminal, std::unordered_set<Operand>> maintain;

  Parser() : grammar{}, threads{}, variables{}, locks{}, maintain{} {}

  EventType parse_event_type(std::string event_type) {
    if (event_type == "R") {
      return EventType::read;
    } else if (event_type == "W") {
      return EventType::write;
    } else if (event_type == "L") {
      return EventType::lock;
    } else if (event_type == "U") {
      return EventType::unlock;
    } else if (event_type == "F") {
      return EventType::fork;
    } else if (event_type == "J") {
      return EventType::join;
    } else if (event_type == "B") {
      return EventType::begin;
    } else if (event_type == "E") {
      return EventType::end;
    }

    std::cerr << "Parser: Invalid event type\n";
    std::exit(EXIT_FAILURE);
  }

  Event parse_event(std::string event) {
    std::vector<std::string> tokens = util::split(event, ",");

    Thread thread = tokens[0];

    EventType event_type = parse_event_type(tokens[1]);

    Operand operand;
    if (event_type != EventType::begin && event_type != EventType::end) {
      operand = tokens[2];
    }

    return Event{thread, event_type, operand, Annotation::undefined};
  }

  void parse_map(std::string map_path) {
    std::ifstream map_file{map_path};

    if (!map_file.is_open()) {
      std::cerr << "Parser: Map file does not exist\n";
      std::exit(EXIT_FAILURE);
    }

    std::string line;
    while (std::getline(map_file, line)) {
      util::trim(line);
      std::vector<std::string> tokens = util::split(line, "|");

      Terminal terminal{"[" + tokens[0] + "]"};
      Event event{parse_event(tokens[1])};

      grammar.terminals.insert(terminal);
      grammar.content[terminal] = event;

      threads.insert(event.thread);
      if (event.type == EventType::read || event.type == EventType::write) {
        variables.insert(event.operand);
      } else if (event.type == EventType::lock ||
                 event.type == EventType::unlock) {
        locks.insert(event.operand);
      } else if (event.type == EventType::fork ||
                 event.type == EventType::join) {
        threads.insert(event.operand);
      }
    }
  }

  void parse_grammar(std::string grammar_path) {
    std::ifstream grammar_file{grammar_path};

    if (!grammar_file.is_open()) {
      std::cerr << "Parser: Grammar file does not exist\n";
      std::exit(EXIT_FAILURE);
    }

    std::string line;
    while (std::getline(grammar_file, line)) {
      util::trim(line);
      std::vector<std::string> tokens = util::split(line, " -> ");

      Nonterminal nonterminal{tokens[0]};
      std::vector<Symbol> symbols = util::split(tokens[1], " ");

      grammar.nonterminals.insert(nonterminal);
      grammar.rules[nonterminal] = symbols;

      for (const auto &symbol : grammar.rules[nonterminal]) {
        if (is_terminal(symbol)) {
          grammar.terminals.insert(symbol);
        } else {
          grammar.nonterminals.insert(symbol);
        }
      }
    }
  }

  void parse_prep(std::string prep_path) {
    std::ifstream prep_file{prep_path};

    if (!prep_file.is_open()) {
      std::cerr << "Parser: Preprocess file does not exist\n";
      std::exit(EXIT_FAILURE);
    }

    std::string line;
    while (std::getline(prep_file, line)) {
      util::trim(line);
      std::vector<std::string> tokens = util::split(line, " -> ");

      Nonterminal nonterminal{tokens[0]};

      if (tokens.size() > 1) {
        std::vector<Operand> operands = util::split(tokens[1], " ");
        maintain[nonterminal].insert(operands.begin(), operands.end());
      } else {
        maintain[nonterminal] = std::unordered_set<Operand>();
      }
    }
  }

  void parse(std::string map_path, std::string grammar_path,
             std::string prep_path) {
    parse_map(map_path);
    parse_grammar(grammar_path);
    parse_prep(prep_path);
  }
};

#endif
