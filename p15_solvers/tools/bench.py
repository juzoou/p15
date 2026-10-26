#!/usr/bin/env python3
"""Простой стенд для сравнения решателей (для разработки).

usage: bench.py <tasks_dir> <out_dir> --algos exact,ida,reduce [--time 10] [--bin ../bin]
Для каждой задачи запускает каждый решатель, проверяет решение независимым
чекером (check.py) и печатает таблицу F; итог -- в <out_dir>/results.csv.
"""
import argparse
import csv
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import check  # noqa: E402


def verify(sol_path, task_path):
    old = sys.argv
    try:
        sys.argv = ['check.py', sol_path, task_path]
        from io import StringIO
        buf, so = StringIO(), sys.stdout
        sys.stdout = buf
        try:
            check.main()
            ok = True
        except SystemExit as e:
            ok = (e.code in (0, None))
        finally:
            sys.stdout = so
        return ok, buf.getvalue().strip()
    finally:
        sys.argv = old


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('tasks')
    ap.add_argument('out')
    ap.add_argument('--algos', default='reduce')
    ap.add_argument('--time', type=float, default=10)
    ap.add_argument('--bin', default=os.path.join(HERE, '..', 'bin'))
    ap.add_argument('--extra', default='')
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    algos = args.algos.split(',')
    tasks = sorted(f for f in os.listdir(args.tasks) if not f.startswith('.'))
    rows = []
    tot = {a: 0 for a in algos}
    print('%-34s' % 'task' + ''.join('%14s' % a for a in algos))
    for t in tasks:
        tp = os.path.join(args.tasks, t)
        line = '%-34s' % t[:34]
        for a in algos:
            exe = os.path.join(args.bin, 'p15_' + a + ('.exe' if os.name == 'nt' else ''))
            sp = os.path.join(args.out, os.path.splitext(t)[0] + '_' + a + '.txt')
            cmd = [exe, tp, sp, '--time', str(args.time)] + (args.extra.split() if args.extra else [])
            t0 = time.time()
            try:
                p = subprocess.run(cmd, capture_output=True, text=True, timeout=args.time * 3 + 30)
                code, outp = p.returncode, p.stdout.strip()
            except subprocess.TimeoutExpired:
                code, outp = -1, ''
            dt = time.time() - t0
            F = None
            status = 'ok'
            if code == 0:
                ok, chk = verify(sp, tp)
                if not ok or chk != outp:
                    status = 'BAD(%s/%s)' % (outp, chk)
                else:
                    F = int(outp)
            else:
                status = 'exit%d' % code
            rows.append(dict(task=t, algo=a, F=F, time=round(dt, 3), status=status))
            if F is not None:
                tot[a] += F
            line += '%14s' % (F if F is not None else status)
        print(line, flush=True)
    print('%-34s' % 'TOTAL' + ''.join('%14d' % tot[a] for a in algos))
    with open(os.path.join(args.out, 'results.csv'), 'w', newline='') as f:
        w = csv.DictWriter(f, fieldnames=['task', 'algo', 'F', 'time', 'status'])
        w.writeheader()
        w.writerows(rows)


if __name__ == '__main__':
    main()
