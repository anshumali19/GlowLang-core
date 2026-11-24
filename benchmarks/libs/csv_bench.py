# The work of csv_bench.glow with Python's csv module (written in C).
import csv, io, time

rows = [["id", "name", "city", "price", "note"]]
for i in range(1, 100001):
    rows.append([str(i), "name" + str(i), "Pune", str(i * 7 % 1000), "has, comma" if i % 10 == 0 else "plain"])

start = time.time()
buf = io.StringIO()
csv.writer(buf, lineterminator="\n").writerows(rows)
text = buf.getvalue()
print(f"{'csv.writer':<16}{int(100001 / (time.time() - start)):,} rows/s")

start = time.time()
back = list(csv.reader(io.StringIO(text)))
print(f"{'csv.reader':<16}{int(100001 / (time.time() - start)):,} rows/s")
assert back == rows
