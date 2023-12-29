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
#include <vector>

struct Parser {
  Grammar grammar;
  std::unordered_set<Thread> threads;
  std::unordered_set<Operand> variables;

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

      switch (symbols.size()) {
      case 1:
        grammar.terminals.insert(symbols[0]);
        break;
      case 2:
        grammar.nonterminals.insert(symbols[0]);
        grammar.nonterminals.insert(symbols[1]);
        break;
      default:
        std::cerr << "Parser: Grammar is not in CNF\n";
        std::exit(EXIT_FAILURE);
      }
    }
  }

  std::tuple<Grammar, std::unordered_set<Thread>, std::unordered_set<Operand>>
  parse(std::string map_path, std::string grammar_path) {
    parse_map(map_path);
    parse_grammar(grammar_path);
    return std::make_tuple(grammar, threads, variables);
  }
};

#endif
