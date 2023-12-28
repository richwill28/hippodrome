#include "event.h"
#include "util.h"
#include <fstream>
#include <iostream>
#include <regex>
#include <string>
#include <unordered_map>

struct Parser {
  std::unordered_map<std::string, int> variable;
  std::unordered_map<std::string, int> lock;
  std::unordered_map<std::string, int> thread;

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

    std::cerr << "Parser: Invalid event type";
    std::exit(EXIT_FAILURE);
  }

  Event parse_event(std::string event) {
    std::vector<std::string> tokens = util::split(event, ",");

    Thread thread = std::stoi(tokens[0]);

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

    std::string event;
    while (std::getline(map_file, event)) {
      std::vector<std::string> tokens = util::split(event, "|");

      std::string id{tokens[0]};
      Event event{parse_event(tokens[1])};

      if (event.type == EventType::read || event.type == EventType::write) {

      } else if (event.type == EventType::lock ||
                 event.type == EventType::unlock) {

      } else if (event.type == EventType::fork ||
                 event.type == EventType::join) {
      }
    }
  }

  void parse_grammar(std::string grammar_path) {
    std::ifstream grammar_file{grammar_path};

    if (!grammar_file.is_open()) {
      std::cerr << "Parser: Grammar file does not exist\n";
      std::exit(EXIT_FAILURE);
    }
  }

  void parse(std::string map_path, std::string grammar_path) {
    parse_map(map_path);
    parse_grammar(grammar_path);
  }
};
