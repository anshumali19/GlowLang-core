l = []
i = 0
while i < 1000000:
    l.append(i * 2)
    i = i + 1
s = 0
for x in l:
    s = s + x
d = {}
i = 0
while i < 200000:
    d["k" + str(i)] = i
    i = i + 1
hits = 0
i = 0
while i < 200000:
    if d["k" + str(i)] == i:
        hits = hits + 1
    i = i + 1
print(s)
print(hits)
