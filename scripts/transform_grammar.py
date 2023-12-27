import common
import sys


class Grammar:
    def __init__(self):
        self.nonterminals: set[str] = set()
        self.terminals: set[str] = set()
        self.rules: dict[str, list[str]] = dict()

    def add_nonterminal(self, n: str):
        self.nonterminals.add(n)

    def add_nonterminals(self, ns: set[str]):
        self.nonterminals = self.nonterminals.union(ns)

    def add_terminal(self, t: str):
        self.terminals.add(t)

    def add_terminals(self, ts: set[str]):
        self.terminals = self.terminals.union(ts)

    def add_rule(self, n: str, ss: list[str]):
        self.rules[n] = ss
        self.nonterminals.add(n)
        for s in ss:
            if is_nonterminal(s):
                self.nonterminals.add(s)
            else:
                self.terminals.add(s)

    def add_rules(self, rs: dict[str, list[str]]):
        for n, ss in rs.items():
            self.add_rule(n, ss)

    def get_fresh_nonterminal(self) -> str:
        i = 0
        while True:
            n = str(i)
            if n not in self.nonterminals:
                return n
            i += 1

    def __str__(self):
        string = ""
        for n, ss in self.rules.items():
            string += n + " -> "
            for s in ss:
                string += s + " "
            string += "\n"
        return string


def is_nonterminal(s: str) -> bool:
    return s.isdigit()


def transform(grammar: Grammar) -> Grammar:
    cnf_grammar = Grammar()
    cnf_grammar.add_nonterminals(grammar.nonterminals)
    cnf_grammar.add_terminals(grammar.terminals)

    # Arrange that all bodies of length 2 or more to consist only of nonterminals.
    prod = {}
    for n, ss in grammar.rules.items():
        body = []
        for s in ss:
            if is_nonterminal(s):
                body.append(s)
            else:
                if s not in prod:
                    nont = cnf_grammar.get_fresh_nonterminal()
                    cnf_grammar.add_rule(nont, [s])
                    body.append(nont)
                    prod[s] = nont
                else:
                    body.append(prod[s])
        cnf_grammar.add_rule(n, body)

    # Break bodies of length 3 or more into a cascade of productions, each with a body consisting of two nonterminals.
    rules = cnf_grammar.rules.copy()
    for n, ss in rules.items():
        body = ss
        while len(body) > 2:
            nont = cnf_grammar.get_fresh_nonterminal()
            cnf_grammar.add_rule(n, [body[0], nont])
            n = nont
            body = body[1:]
        cnf_grammar.add_rule(n, body)

    return cnf_grammar


benchmarks = common.benchmarks if len(sys.argv) == 1 else sys.argv[1:]

for b in benchmarks:
    if b not in common.benchmarks:
        print(f"Invalid benchmark {b}, skipping...")

    print(f"Processing benchmark {b}...")

    benchmark_path = f"{common.BENCHMARKS_PATH}/{b}"
    grammar_path = f"{benchmark_path}/grammar.txt"
    cnf_grammar_path = f"{benchmark_path}/cnf_grammar.txt"

    grammar = Grammar()

    with open(grammar_path, "r") as file:
        for line in file:
            line = line.split()
            nonterminal = line[0]
            symbols = line[2:]
            grammar.add_rule(nonterminal, symbols)

    with open(cnf_grammar_path, "w") as file:
        cnf_grammar = transform(grammar)
        file.write(str(cnf_grammar))

    print(f"CNF grammar generated: {cnf_grammar_path}")
