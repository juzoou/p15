#!/usr/bin/env python3
"""Независимая проверка файла решения (DATA + COST + ORDER) и подсчёт F.

usage: check.py <solution> [<task>]
Если указан файл задачи (любой из двух форматов), проверяется, что DATA и COST
в решении совпадают с задачей. Печатает F; код возврата 0 -- решение корректно.
"""
import sys


def tokens(text):
    out = []
    for no, line in enumerate(text.splitlines(), 1):
        line = line.split('--', 1)[0].replace('/', ' / ')
        out += [(t, no) for t in line.split()]
    return out


def parse(path):
    text = open(path, encoding='utf-8', errors='replace').read()
    T = tokens(text)
    if not any(t.upper() in ('DATA', 'COST', 'ORDER', 'SIZE') for t, _ in T):
        v = [int(t) for t, _ in T]          # упрощённый формат
        w, h = v[4], v[5]
        cells = v[6:6 + w * h]
        return dict(R=h, C=w, cells=cells, cost=v[:4], order=None)
    res = dict(cost=[1, 1, 1, 1], order=None)
    i = 0
    while i < len(T):
        kw = T[i][0].upper(); i += 1
        if kw == 'DATA':
            assert T[i][0].upper() == 'SIZE'
            R, C = int(T[i + 1][0]), int(T[i + 2][0]); i += 3
            cells = []
            while i < len(T) and T[i][0] != '/' and T[i][0].upper() not in ('COST', 'ORDER'):
                cells.append(0 if T[i][0] == '=' else int(T[i][0])); i += 1
            if i < len(T) and T[i][0] == '/':
                i += 1
            assert len(cells) == R * C, 'wrong number of cells'
            res.update(R=R, C=C, cells=cells)
        elif kw == 'COST':
            res['cost'] = [int(T[i + k][0]) for k in range(4)]; i += 4
            if i < len(T) and T[i][0] == '/':
                i += 1
        elif kw == 'ORDER':
            order = []
            while i < len(T) and T[i][0] != '/':
                order.append((int(T[i][0]), T[i + 1][0].upper())); i += 2
            i += 1
            res['order'] = order
        else:
            raise ValueError('unexpected token %r at line %d' % T[i - 1])
    return res


def main():
    sol = parse(sys.argv[1])
    R, C, a = sol['R'], sol['C'], list(sol['cells'])
    N = R * C
    assert sorted(a) == list(range(N)), 'DATA is not a permutation'
    if len(sys.argv) > 2:
        task = parse(sys.argv[2])
        assert (task['R'], task['C'], task['cells']) == (R, C, sol['cells']), 'DATA differs from the task'
        assert task['cost'] == sol['cost'], 'COST differs from the task'
    cost = sol['cost']
    delta = {'U': (-1, 0), 'D': (1, 0), 'L': (0, -1), 'R': (0, 1)}
    didx = {'U': 0, 'D': 1, 'L': 2, 'R': 3}
    pos = {v: i for i, v in enumerate(a)}
    total = 0
    for k, (t, d) in enumerate(sol['order'] or [], 1):
        if t not in pos or t == 0:
            print('move %d: no tile %d' % (k, t)); sys.exit(1)
        p = pos[t]
        r, c = divmod(p, C)
        dr, dc = delta[d]
        nr, nc = r + dr, c + dc
        if not (0 <= nr < R and 0 <= nc < C) or a[nr * C + nc] != 0:
            print('move %d: tile %d cannot move %s' % (k, t, d)); sys.exit(1)
        q = nr * C + nc
        a[q], a[p] = t, 0
        pos[t], pos[0] = q, p
        total += cost[didx[d]]
    P = sum(1 for i in range(N - 1) if a[i] == i + 1)
    print(100 * P - total)


if __name__ == '__main__':
    main()
