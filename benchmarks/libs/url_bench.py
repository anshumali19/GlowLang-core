# The work of url_bench.glow with Python's urllib.parse (written in Python).
import time
from urllib.parse import urlsplit, parse_qs, quote, urlencode, urljoin

def measure(label, f):
    n = 0
    start = time.time()
    while time.time() - start < 2:
        f()
        n += 1
    print(f"{label:<14}{int(n / (time.time() - start)):,} calls/s")

# 1,000 different links: urlsplit keeps the last 128 results in a cache.
links = [f"https://ana@shop.example:8443/cart/items/{i}?id={i}&sort=price#top" for i in range(1000)]
params = {"q": "tea & cake", "page": "2", "lang": "en"}
k = 0

def parse():
    # url.parse also gives the port and the decoded query.
    global k
    k = (k + 1) % 1000
    p = urlsplit(links[k])
    return p.port, parse_qs(p.query)

measure("urlsplit", parse)
measure("quote", lambda: quote("tea & cake / café", safe=""))
measure("urlencode", lambda: urlencode(params))
def join():
    global k
    k = (k + 1) % 1000
    return urljoin(links[k], "../../g?y#s")

measure("urljoin", join)
