#!/usr/bin/env python3
"""Генератор тестовых задач.

usage: gen.py <out_dir> [--sizes 3x3,4x4,...] [--count 10] [--kinds random,solvable,walk100]
              [--costs 1111,3111,...] [--format data|simple] [--seed 1]

kind:  random   -- случайная перестановка (половина неразрешима)
       solvable -- случайная разрешимая позиция
       walkL    -- L случайных ходов от собранного поля (без немедленных возвратов)
"""
import argparse
import os
import random


def goal(R, C):
    return list(range(1, R * C)) + [0]


def parity_class(R, C, a):
    N = R * C
    seen = [False] * N
    cycles = 0
    for i in range(N):
        if not seen[i]:
            cycles += 1
            j = i
            while not seen[j]:
                seen[j] = True
                v = a[j] if a[j] else N
                j = v - 1
    b = a.index(0)
    return ((N - cycles) + (R - 1 - b // C) + (C - 1 - b % C)) & 1


def make(R, C, kind, rng):
    a = goal(R, C)
    if kind.startswith('walk'):
        L = int(kind[4:])
        b = R * C - 1
        last = None
        for _ in range(L):
            opts = []
            for d, (dr, dc) in enumerate(((-1, 0), (1, 0), (0, -1), (0, 1))):
                r, c = b // C + dr, b % C + dc
                if 0 <= r < R and 0 <= c < C and (last is None or d != last ^ 1):
                    opts.append((d, r * C + c))
            if not opts:   # тупик (поле-линия) -- разрешаем возврат
                for d, (dr, dc) in enumerate(((-1, 0), (1, 0), (0, -1), (0, 1))):
                    r, c = b // C + dr, b % C + dc
                    if 0 <= r < R and 0 <= c < C:
                        opts.append((d, r * C + c))
            if not opts:
                break
            d, n = rng.choice(opts)
            a[b], a[n] = a[n], 0
            b, last = n, d
        return a
    rng.shuffle(a)
    if kind == 'solvable' and parity_class(R, C, a) != 0:
        i, j = [k for k in range(R * C) if a[k]][:2]
        a[i], a[j] = a[j], a[i]
    return a


def write(path, R, C, a, cost, fmt):
    with open(path, 'w', newline='\n') as f:
        if fmt == 'simple':
            f.write(' '.join(map(str, cost + [C, R])) + '\n')
            for r in range(R):
                f.write(' '.join(str(v) for v in a[r * C:(r + 1) * C]) + '\n')
        else:
            f.write('DATA\nSIZE %d %d\n' % (R, C))
            for r in range(R):
                f.write(' '.join('=' if v == 0 else str(v) for v in a[r * C:(r + 1) * C]) + '\n')
            f.write('/\nCOST\n%s /\n' % ' '.join(map(str, cost)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('out')
    ap.add_argument('--sizes', default='3x3,4x4')
    ap.add_argument('--count', type=int, default=10)
    ap.add_argument('--kinds', default='random')
    ap.add_argument('--costs', default='1111')
    ap.add_argument('--format', default='data')
    ap.add_argument('--seed', type=int, default=1)
    args = ap.parse_args()
    rng = random.Random(args.seed)
    os.makedirs(args.out, exist_ok=True)
    for size in args.sizes.split(','):
        R, C = map(int, size.lower().split('x'))
        for kind in args.kinds.split(','):
            for cs in args.costs.split(','):
                cost = [int(x) for x in cs.split(':')] if ':' in cs else [int(x) for x in cs]
                for k in range(args.count):
                    a = make(R, C, kind, rng)
                    name = '%dx%d_%s_c%s_%02d.txt' % (R, C, kind, cs.replace(':', '-'), k)
                    write(os.path.join(args.out, name), R, C, a, cost, args.format)


if __name__ == '__main__':
    main()
