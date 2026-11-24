# The page from template_bench.glow built by hand in Python, for comparison.
import html, time

rows = [{"id": i, "name": f"item <{i}>", "price": i * 3, "sale": i % 7 == 0} for i in range(100)]
data = {"title": "Price list", "rows": rows}

def by_hand(data):
    out = ["<h1>", html.escape(data["title"]), "</h1>\n<table>\n"]
    for r in data["rows"]:
        out.append(f"<tr><td>{r['id']}</td><td>{html.escape(r['name'])}</td><td>{r['price']}</td>")
        if r["sale"]:
            out.append("<td>sale</td>")
        out.append("</tr>\n")
    out.append("</table>\n")
    return "".join(out)

n = 0
start = time.time()
while time.time() - start < 2:
    by_hand(data)
    n += 1
print(f"{'Python by hand':<24}{int(n / (time.time() - start)):,} renders/s")
