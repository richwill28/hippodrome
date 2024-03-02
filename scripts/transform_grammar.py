from collections import deque
import common
import sys


class Grammar:
    def __init__(self):
        self.nonterminals: set[str] = set()
        self.terminals: set[str] = set()
        self.rules: dict[str, deque[str]] = dict()
        self.fresh_idx: int = 0

    def add_nonterminal(self, n: str):
        self.nonterminals.add(n)
        self.fresh_idx = int(n) + 1 if int(n) >= self.fresh_idx else self.fresh_idx

    def add_nonterminals(self, ns: set[str]):
        for s in ns:
            self.add_nonterminal(s)

    def add_terminal(self, t: str):
        self.terminals.add(t)

    def add_terminals(self, ts: set[str]):
        self.terminals = self.terminals.union(ts)

    def add_rule(self, n: str, ss: deque[str]):
        self.rules[n] = ss
        self.nonterminals.add(n)
        for s in ss:
            if is_nonterminal(s):
                self.nonterminals.add(s)
            else:
                self.terminals.add(s)

    def add_rules(self, rs: dict[str, deque[str]]):
        for n, ss in rs.items():
            self.add_rule(n, ss)

    def get_fresh_nonterminal(self) -> str:
        fresh_nont = str(self.fresh_idx)
        self.fresh_idx += 1
        return fresh_nont

    def write(self, path: str):
        with open(path, "w") as file:
            for n, ss in self.rules.items():
                string = ""
                string += n + " -> "
                for s in ss:
                    string += s + " "
                string += "\n"
                file.write(string)

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
    print("Transforming grammar...")

    cnf_grammar = Grammar()
    cnf_grammar.add_nonterminals(grammar.nonterminals)
    cnf_grammar.add_terminals(grammar.terminals)

    # Arrange that all bodies of length 2 or more to consist only of nonterminals.
    print("Phase 1")
    th = 1
    prod = {}
    for n, ss in grammar.rules.items():
        body = deque()
        for s in ss:
            if is_nonterminal(s):
                body.append(s)
            else:
                if s not in prod:
                    nont = cnf_grammar.get_fresh_nonterminal()
                    term = deque()
                    term.append(s)
                    cnf_grammar.add_rule(nont, term)
                    body.append(nont)
                    prod[s] = nont
                else:
                    body.append(prod[s])
        cnf_grammar.add_rule(n, body)
        print(f"{th}/{len(grammar.rules)} rule processed")
        th += 1

    print()

    # Break bodies of length 3 or more into a cascade of productions, each with a body consisting of two nonterminals.
    print("Phase 2")
    th = 1
    rules = cnf_grammar.rules.copy()
    for n, ss in rules.items():
        while len(ss) > 2:
            # print(f"body len = {len(body)}")
            nont = cnf_grammar.get_fresh_nonterminal()
            cnf_grammar.add_rule(n, [ss.popleft(), nont])
            n = nont
        cnf_grammar.add_rule(n, ss)
        print(f"{th}/{len(rules)} rule processed")
        th += 1

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
        print("Processing grammar...")
        for line in file:
            line = line.split()
            nonterminal = line[0]
            symbols = line[2:]
            grammar.add_rule(nonterminal, deque(symbols))

    # with open(cnf_grammar_path, "w") as file:
    #     cnf_grammar = transform(grammar)
    #     file.write(str(cnf_grammar))

    cnf_grammar = transform(grammar)
    cnf_grammar.write(cnf_grammar_path)

    print(f"CNF grammar generated: {cnf_grammar_path}")
