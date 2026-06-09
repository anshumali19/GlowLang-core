echo "Running Benchmarks..."

echo "GlowLang:"
time bin/glowlang run fib35.glow

echo "Python:"
cat << 'PY' > fib35.py
def fib(n):
    if n < 2: return n
    return fib(n-1) + fib(n-2)
print(fib(35))
PY
time python3 fib35.py

echo "Node.js:"
cat << 'JS' > fib35.js
function fib(n) {
    if (n < 2) return n;
    return fib(n-1) + fib(n-2);
}
console.log(fib(35));
JS
time node fib35.js

echo "Ruby:"
cat << 'RB' > fib35.rb
def fib(n)
  if n < 2
    return n
  end
  return fib(n-1) + fib(n-2)
end
puts fib(35)
RB
time ruby fib35.rb

echo "PHP:"
cat << 'PHP' > fib35.php
<?php
function fib($n) {
    if ($n < 2) return $n;
    return fib($n-1) + fib($n-2);
}
echo fib(35) . "\n";
?>
PHP
time php fib35.php

