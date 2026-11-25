# The work of textwrap_bench.glow with Python's textwrap (written in Python).
# The words are made by Python's own random numbers, so they differ from
# GlowLang's, but they have the same lengths: 1 to 10 letters.
import random, textwrap, time

random.seed(7)
words = ["".join(random.choice("abcdefghij") for _ in range(random.randint(1, 10))) for _ in range(2000)]
text = " ".join(words)
sentence = " ".join(words[:40])
assert len(textwrap.wrap(text, 70)) > 100

def measure(label, f):
    n = 0
    start = time.time()
    while time.time() - start < 2:
        f()
        n += 1
    print(f"{label:<18}{n / (time.time() - start):,.1f} calls/s")

measure("wrap 2,000 words", lambda: textwrap.wrap(text, 70))
measure("shorten", lambda: textwrap.shorten(sentence, 60))
