#!/usr/bin/env python3
"""Build a folder of puzzle tests by calling gen.py (via subprocess).

WHAT IT DOES
  For every board size, it makes several "weight sets" (the 4 move costs).
  For every weight set, it makes several boards. So:

      number of files = (number of sizes) x (weight sets) x (boards per set)

  Example: 12 sizes x 4 weight sets x 3 boards = 144 files.

  Weight set 1 is always 1111. The others are random.
  Boards in the same weight set have the SAME weights but DIFFERENT layouts.

OUTPUT
  <out_dir>/<size>_<kind>_c<costs>_<nn>.txt   the test files (made by gen.py)
  <out_dir>/index.csv                         one line per test file

EXAMPLES
  python3 make_tests.py tests
  python3 make_tests.py tests --sizes 3x3,10x10,100x100 --weight-sets 5 --boards 4
  python3 make_tests.py tests --kind walk100 --file-format simple --seed 7
"""
import argparse
import csv
import os
import random
import subprocess
import sys

DEFAULT_SIZES = "3x3,4x4,5x5,3x5,2x8,8x8,10x10,15x15,30x30,50x50,2x100,100x100"
NUM_COSTS = 4      # how many cost numbers per test
MAX_SIDE = 100     # largest allowed width/height

CSV_COLUMNS = ["file", "rows", "cols", "kind",
               "cost1", "cost2", "cost3", "cost4",
               "weight_set", "board", "solvable", "file_format", "seed"]


def parse_size(text):
    rows, cols = map(int, text.lower().split("x"))
    if not (2 <= rows <= MAX_SIDE and 2 <= cols <= MAX_SIDE):
        sys.exit("bad size %s: each side must be 2..%d" % (text, MAX_SIDE))
    return rows, cols


def cost_arg(cost):
    """Format costs the way gen.py wants: '1111' if all <= 9, else '1:20:3:4'."""
    if all(c <= 9 for c in cost):
        return "".join(map(str, cost))
    return ":".join(map(str, cost))


def make_weight_sets(rng, how_many, max_cost):
    """First set is 1,1,1,1. The rest are random and all different."""
    sets = [(1,) * NUM_COSTS]
    attempts = 0
    while len(sets) < how_many and attempts < 10000:
        attempts += 1
        top = rng.choice([9, max_cost])     # mix small and large costs
        candidate = tuple(rng.randint(1, top) for _ in range(NUM_COSTS))
        if candidate not in sets:
            sets.append(candidate)
    return sets


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(
        description="Build a folder of puzzle tests by calling gen.py. "
                    "Also writes index.csv listing every test.",
        epilog="Total files = sizes x weight-sets x boards.")
    ap.add_argument("out_dir", help="folder for the tests (created if missing)")
    ap.add_argument("--gen", default=os.path.join(here, "gen.py"),
                    help="path to gen.py (default: next to this script)")
    ap.add_argument("--sizes", default=DEFAULT_SIZES,
                    help="comma list of ROWSxCOLS, each side 2..100 "
                         "(default: %(default)s)")
    ap.add_argument("--weight-sets", type=int, default=4,
                    help="different cost sets per size, the first is 1111 "
                         "(default: %(default)s)")
    ap.add_argument("--boards", type=int, default=3,
                    help="boards per cost set; they share the same costs "
                         "(default: %(default)s)")
    ap.add_argument("--kind", default="solvable",
                    help="solvable | random | walkN, e.g. walk100 "
                         "(default: %(default)s)")
    ap.add_argument("--max-cost", type=int, default=20,
                    help="largest random cost value (default: %(default)s)")
    ap.add_argument("--file-format", default="data", choices=["data", "simple"],
                    help="format of the test files (default: %(default)s)")
    ap.add_argument("--seed", type=int, default=1,
                    help="same seed gives the same tests (default: %(default)s)")
    args = ap.parse_args()

    rng = random.Random(args.seed)
    os.makedirs(args.out_dir, exist_ok=True)
    rows = []
    weight_set_id = 0

    for size in args.sizes.split(","):
        r, c = parse_size(size)
        for cost in make_weight_sets(rng, args.weight_sets, args.max_cost):
            cs = cost_arg(cost)
            call_seed = rng.randrange(1, 2**31)   # new seed => new boards
            subprocess.run([sys.executable, args.gen, args.out_dir,
                            "--sizes", "%dx%d" % (r, c),
                            "--kinds", args.kind,
                            "--costs", cs,
                            "--count", str(args.boards),
                            "--format", args.file_format,
                            "--seed", str(call_seed)], check=True)

            for b in range(args.boards):
                name = "%dx%d_%s_c%s_%02d.txt" % (r, c, args.kind,
                                                  cs.replace(":", "-"), b)
                if not os.path.isfile(os.path.join(args.out_dir, name)):
                    sys.exit("gen.py did not create " + name)
                solvable = "no_idea" if args.kind == "random" else "yes"
                rows.append([name, r, c, args.kind, *cost,
                             weight_set_id, b, solvable,
                             args.file_format, call_seed])
            weight_set_id += 1

    with open(os.path.join(args.out_dir, "index.csv"), "w",
              newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(CSV_COLUMNS)
        w.writerows(rows)

    print("Created %d tests in %s" % (len(rows), args.out_dir))
    print("Lookup file: %s" % os.path.join(args.out_dir, "index.csv"))


if __name__ == "__main__":
    main()