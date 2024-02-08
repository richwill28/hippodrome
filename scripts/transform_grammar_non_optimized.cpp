#include <algorithm>
#include <cctype>
#include <deque>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace util {
void ltrim(std::string &str) {
  str.erase(str.begin(),
            std::find_if(str.begin(), str.end(),
                         [](unsigned char ch) { return !std::isspace(ch); }));
}

void rtrim(std::string &str) {
  str.erase(std::find_if(str.rbegin(), str.rend(),
                         [](unsigned char ch) { return !std::isspace(ch); })
                .base(),
            str.end());
}

void trim(std::string &str) {
  ltrim(str);
  rtrim(str);
}

std::deque<std::string> split(std::string str, std::string delim) {
  std::deque<std::string> res;

  size_t pos_begin = 0;
  size_t pos_end;
  std::string token;

  while ((pos_end = str.find(delim, pos_begin)) != std::string::npos) {
    token = str.substr(pos_begin, pos_end - pos_begin);
    pos_begin = pos_end + delim.length();
    res.push_back(token);
  }
  res.push_back(str.substr(pos_begin));

  return res;
}
} // namespace util

using Symbol = std::string;
using Nonterminal = std::string;
using Terminal = std::string;

bool is_nonterminal(const std::string &s) {
  // return !s.empty() && std::all_of(s.begin(), s.end(), [](unsigned char c) {
  //   return std::isdigit(c);
  // });
  return s.substr(0, 1) != "[";
}

struct Grammar {
  std::unique_ptr<std::unordered_set<Nonterminal>> nonterminals;
  std::unique_ptr<std::unordered_set<Terminal>> terminals;
  std::unique_ptr<std::unordered_map<Nonterminal, std::deque<Symbol>>> rules;
  unsigned long long fresh_nonterminal_idx;

  Grammar()
      : nonterminals{std::make_unique<std::unordered_set<Nonterminal>>()},
        terminals{std::make_unique<std::unordered_set<Terminal>>()},
        rules{std::make_unique<
            std::unordered_map<Nonterminal, std::deque<Symbol>>>()},
        fresh_nonterminal_idx{0} {}

  void add_nonterminal(Nonterminal n) {
    nonterminals->insert(n);
    fresh_nonterminal_idx = std::stoull(n) >= fresh_nonterminal_idx
                                ? std::stoull(n) + 1
                                : fresh_nonterminal_idx;
  }

  void add_terminal(Terminal t) { terminals->insert(t); }

  void add_rule(Nonterminal n, std::unique_ptr<std::deque<Symbol>> ss) {

    (*rules)[n] = *ss;
    add_nonterminal(n);
    for (Symbol s : *ss) {
      if (is_nonterminal(s)) {
        add_nonterminal(s);
      } else {
        add_terminal(s);
      }
    }
  }

  Nonterminal get_fresh_nonterminal() {
    return std::to_string(fresh_nonterminal_idx++);
  }

  void transform_to_cnf() {
    std::cout << "Transforming grammar...\n";

    std::cout << "Phase 1\n";
    unsigned long long th = 1;
    // Arrange such that all bodies of length 2 or more consist only of
    // nonterminals.
    auto _rules =
        std::make_unique<std::unordered_map<Nonterminal, std::deque<Symbol>>>();
    *_rules = *rules;
    auto prod = std::make_unique<std::unordered_map<Terminal, Nonterminal>>();
    for (auto [n, ss] : *_rules) {
      auto body = std::make_unique<std::deque<Nonterminal>>();
      for (Symbol s : ss) {
        if (is_nonterminal(s)) {
          body->push_back(s);
        } else {
          if (!prod->contains(s)) {
            Nonterminal nont{get_fresh_nonterminal()};
            (*prod)[s] = nont;
            auto term = std::make_unique<std::deque<Nonterminal>>();
            term->push_back(s);
            add_rule(nont, std::move(term));
            body->push_back(nont);
          } else {
            body->push_back((*prod)[s]);
          }
        }
      }
      add_rule(n, std::move(body));
      std::cout << th++ << "/" << (*_rules).size() << " rules processed\n";
    }

    std::cout << "Phase 2\n";
    th = 1;
    // Break bodies of length 3 or more into a cascade of productions, each with
    // a body consisting of two nonterminals.
    *_rules = *rules;
    for (auto [_n, ss] : *_rules) {
      Nonterminal n{_n};
      while (ss.size() >= 3) {
        Nonterminal nont{get_fresh_nonterminal()};
        auto body = std::make_unique<std::deque<Nonterminal>>();
        body->push_back(ss.front());
        body->push_back(nont);
        add_rule(n, std::move(body));
        ss.pop_front();
        n = nont;
      }
      auto body = std::make_unique<std::deque<Nonterminal>>();
      *body = ss;
      add_rule(n, std::move(body));
      std::cout << th++ << "/" << _rules->size() << " rules processed\n";
    }
  }

  void parse(const std::string &path) {
    std::cout << "Parsing grammar...\n";

    std::ifstream file{path};

    if (!file.is_open()) {
      std::cerr << "Parser: Grammar file does not exist\n";
      std::exit(EXIT_FAILURE);
    }

    std::string line;
    while (std::getline(file, line)) {
      util::trim(line);
      auto tokens = std::make_unique<std::deque<Symbol>>();
      *tokens = util::split(line, " -> ");
      Nonterminal n{(*tokens)[0]};
      auto ss = std::make_unique<std::deque<Symbol>>();
      *ss = util::split((*tokens)[1], " ");
      add_rule(n, std::move(ss));
    }
  }

  void write(const std::string &path) {
    std::cout << "Writing grammar...\n";

    std::ofstream file{path};
    for (auto &[n, ss] : *rules) {
      file << n << " -> ";
      for (auto &s : ss) {
        file << s << " ";
      }
      file << "\n";
    }
  }

  void print_aux(const std::unordered_map<Terminal, std::string> &event,
                 Nonterminal nonterminal) {
    for (auto &s : (*rules)[nonterminal]) {
      if (event.contains(s)) {
        std::cout << event.at(s) << "\n";
      } else {
        print_aux(event, s);
      }
    }
  }

  void print(std::string map_path, Nonterminal nonterminal) {
    std::unordered_map<Terminal, std::string> event;

    std::ifstream file{map_path};

    if (!file.is_open()) {
      std::cerr << "Parser: Grammar file does not exist\n";
      std::exit(EXIT_FAILURE);
    }

    std::string line;
    while (std::getline(file, line)) {
      util::trim(line);
      auto tokens = std::make_unique<std::deque<Symbol>>();
      *tokens = util::split(line, "|");
      event["[" + (*tokens)[0] + "]"] = (*tokens)[1];
    }

    print_aux(event, nonterminal);
  }
};

std::ostream &operator<<(std::ostream &os, const Grammar &g) {
  for (auto &[n, ss] : *(g.rules)) {
    os << n << " -> ";
    for (auto &s : ss) {
      os << s << " ";
    }
    os << "\n";
  }
  return os;
}

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "Usage: ./transform_grammar <benchmark>\n";
    std::exit(EXIT_FAILURE);
  }
  std::string benchmark{argv[1]};
  // std::string benchmark_path{"/home/richwill/repo/hippodrome/benchmarks/" +
  //                            benchmark};
  std::string benchmark_path{
      "/home/users/nus/e0550368/repo/hippodrome/benchmarks/" + benchmark};
  std::string grammar_path{benchmark_path + "/grammar.txt"};
  std::string cnf_grammar_path{benchmark_path + "/cnf_grammar.txt"};
  std::string map_path{benchmark_path + "/map.txt"};

  std::cout << "Processing benchmark " << benchmark << "...\n";

  Grammar grammar;
  grammar.parse(grammar_path);
  grammar.transform_to_cnf();

  // grammar.write(cnf_grammar_path);
  std::cout << grammar;

  // For checking correctness
  // grammar.print(map_path, "0");
}
