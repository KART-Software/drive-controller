#!/usr/bin/env python3
"""2 つの CSV (decode_log.py と console のログビューアの出力) が同じ値か比べる。

    python3 tools/compare_csv.py a.csv b.csv

列名と行数が同じで、各セルが数値として等しい (float に出来るものは float で、空は空で) ことを確かめる。
数値の表記 (1.0 と 1、1e-05 と 0.00001、改行コード) の違いは無視する。最初の不一致を出して終了コード 1。
"""
import csv
import sys


def norm(cell):
    if cell == "":
        return ""
    try:
        return float(cell)
    except ValueError:
        return cell


def main(a_path, b_path):
    with open(a_path, newline="") as fa, open(b_path, newline="") as fb:
        a = csv.reader(fa)
        b = csv.reader(fb)
        ha, hb = next(a), next(b)
        if ha != hb:
            print(f"columns differ:\n  {a_path}: {ha}\n  {b_path}: {hb}")
            return 1
        n = 0
        for ra, rb in zip(a, b):
            n += 1
            for col, (x, y) in enumerate(zip(ra, rb)):
                if norm(x) != norm(y):
                    print(f"row {n} column {ha[col]!r}: {x!r} != {y!r}")
                    return 1
            if len(ra) != len(rb):
                print(f"row {n}: {len(ra)} vs {len(rb)} cells")
                return 1
        ra, rb = next(a, None), next(b, None)
        if ra is not None or rb is not None:
            print(f"row count differs after {n} rows ({a_path} {'has more' if ra else 'ended'})")
            return 1
    print(f"identical: {n} rows, {len(ha)} columns")
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(2)
    sys.exit(main(sys.argv[1], sys.argv[2]))
