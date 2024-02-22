from collections import deque
import common
import sys


def is_nonterminal(s: str) -> bool:
    return s.isdigit()


def is_terminal(s : str) -> bool:
    return not is_nonterminal(s)


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


def optimize(grammar: Grammar) -> Grammar:
    print("Optimizing grammar...")

    opt_grammar = Grammar()
    opt_grammar.add_nonterminals(grammar.nonterminals)
    opt_grammar.add_terminals(grammar.terminals)

    # Arrange such that each body of a production rule consist only of either terminals or nonterminals.
    print("Phase 1")
    th = 1

    for nont, symbols in grammar.rules.items():
        is_each_symb_terminal = True
        is_each_symb_nonterminal = True

        terms = deque()
        nonterms = deque()

        for symb in symbols:
            if is_terminal(symb):
                is_each_symb_nonterminal = False
                terms.append(symb) 

            else:
                is_each_symb_terminal = False
                if len(terms) > 0:
                    new_nont = opt_grammar.get_fresh_nonterminal()
                    opt_grammar.add_rule(new_nont, terms)
                    terms = deque()
                    nonterms.append(new_nont)
                nonterms.append(symb)

        if is_each_symb_terminal:
            opt_grammar.add_rule(nont, symbols)
            continue

        if is_each_symb_nonterminal:
            opt_grammar.add_rule(nont, symbols)
            continue

        if len(terms) > 0:
            new_nont = opt_grammar.get_fresh_nonterminal()
            opt_grammar.add_rule(new_nont, terms)
            nonterms.append(new_nont)

        if len(nonterms) > 0:
            opt_grammar.add_rule(nont, nonterms)

        print(f"{th}/{len(grammar.rules)} rules processed")
        th += 1

    # Break bodies of length 3 or more into a cascade of productions, each with a body consisting of two nonterminals.
    # print("Phase 2")
    # th = 1

    # rules = opt_grammar.rules.copy()

    # for nont, symbols in rules.items():
    #     is_each_symb_nonterminal = False

    #     for symb in symbols:
    #         # Based on Phase 1 post-condition
    #         if is_nonterminal(symb):
    #             is_each_symb_nonterminal = True
    #         break

    #     if is_each_symb_nonterminal:
    #         while len(symbols) > 2:
    #             new_nont = opt_grammar.get_fresh_nonterminal()
    #             opt_grammar.add_rule(nont, deque([symbols.popleft(), new_nont]))
    #             nont = new_nont

    #         opt_grammar.add_rule(nont, symbols)

    #     print(f"{th}/{len(rules)} rules processed")
    #     th += 1

    return opt_grammar


benchmarks = common.benchmarks if len(sys.argv) == 1 else sys.argv[1:]

for b in benchmarks:
    if b not in common.benchmarks:
        print(f"Invalid benchmark {b}, skipping...")

    print(f"Processing benchmark {b}...")

    benchmark_path = f"{common.BENCHMARKS_PATH}/{b}"
    grammar_path = f"{benchmark_path}/grammar.txt"
    opt_grammar_path = f"{benchmark_path}/opt_grammar.txt"

    grammar = Grammar()

    with open(grammar_path, "r") as file:
        print("Processing grammar...")

        for line in file:
            line = line.split()
            nonterminal = line[0]
            symbols = line[2:]
            grammar.add_rule(nonterminal, symbols)

    opt_grammar = optimize(grammar)
    opt_grammar.write(opt_grammar_path)

    print(f"Optimized grammar generated: {opt_grammar_path}")
