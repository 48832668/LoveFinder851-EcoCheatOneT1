import csv, sys
path, lo, hi = sys.argv[1], float(sys.argv[2]), float(sys.argv[3])
with open(path, newline='', encoding='utf-8', errors='replace') as f:
    r = csv.reader(f); next(r, None)
    for row in r:
        if len(row) < 3: continue
        try: t = float(row[1]) / 1e6
        except ValueError: continue
        if lo <= t <= hi:
            print('%11.4f  %s' % (t, row[2].strip()))
