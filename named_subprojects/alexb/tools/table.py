#!/usr/bin/env python3
"""Сводная таблица (markdown) по одному или нескольким results.csv от bench.py.

usage: table.py results1.csv [results2.csv ...]
Лучший результат в строке выделяется жирным; в конце -- число "побед" алгоритмов.
"""
import csv
import sys
from collections import OrderedDict


def main():
    rows = []
    for p in sys.argv[1:]:
        rows += list(csv.DictReader(open(p)))
    algos = []
    for r in rows:
        if r['algo'] not in algos:
            algos.append(r['algo'])
    data = OrderedDict()
    for r in rows:
        data.setdefault(r['task'], {})[r['algo']] = r

    def key(t):
        size = t.split('_')[0]
        a, b = size.split('x')
        return (int(a) * int(b), t)

    wins = {a: 0 for a in algos}
    print('| задача | ' + ' | '.join(algos) + ' |')
    print('|---|' + '---:|' * len(algos))
    for t in sorted(data, key=key):
        vals = {}
        for a in algos:
            r = data[t].get(a)
            vals[a] = int(r['F']) if r and r['F'] not in ('', 'None') else None
        best = max([v for v in vals.values() if v is not None], default=None)
        cells = []
        for a in algos:
            v = vals[a]
            if v is None:
                st = data[t].get(a, {}).get('status', '')
                cells.append('—' if st.startswith('exit5') else (st or '—'))
            elif v == best:
                cells.append('**%d**' % v)
                wins[a] += 1
            else:
                cells.append(str(v))
        name = t.rsplit('.', 1)[0].replace('_00', '')
        print('| %s | %s |' % (name, ' | '.join(cells)))
    print()
    print('Лучший результат (с учётом ничьих): ' + ', '.join('%s — %d' % (a, wins[a]) for a in algos))


if __name__ == '__main__':
    main()
