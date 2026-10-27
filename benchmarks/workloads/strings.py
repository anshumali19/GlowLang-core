parts = []
i = 0
while i < 200000:
    parts.append("item" + str(i))
    i = i + 1
total = 0
for p in parts:
    total = total + len(p)
csv = "a,b,c,d,e,f,g,h"
n = 0
i = 0
while i < 50000:
    n = n + len(csv.split(","))
    i = i + 1
print(total)
print(n)
