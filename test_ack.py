import sys
sys.setrecursionlimit(64000)
class Ackermann:
    def calculate(self, m, n):
        if m == 0:
            return n + 1
        if m > 0 and n == 0:
            return self.calculate(m - 1, 1)
        return self.calculate(m - 1, self.calculate(m, n - 1))

a = Ackermann()
print(a.calculate(3, 8))
