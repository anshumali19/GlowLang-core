# Used by tests/bridge_test.glow.
def greet(name, punct="!"):
    return "hello " + name + punct


class Counter:
    def __init__(self, start=0):
        self.n = start

    def add(self, k=1):
        self.n += k
        return self.n
