/* GlowLang Playground — WASM Runtime */
(function () {
    'use strict';

    const EXAMPLES = {
        hello: `show "Hello from GlowLang!"
show "Welcome to the playground."`,
        fibonacci: `func fib(n):
    when n <= 1:
        return n
    end
    return fib(n - 1) + fib(n - 2)
end

every i in from 0 to 10:
    show "fib(" + i + ") = " + fib(i)
end`,
        factorial: `func factorial(n):
    when n <= 1:
        return 1
    end
    return n * factorial(n - 1)
end

every i in from 1 to 10:
    show i + "! = " + factorial(i)
end`,
        list_ops: `nums = [10, 20, 30, 40, 50]
show "Original: " + nums
nums.push(60)
show "After push: " + nums
show "Length: " + len(nums)
show "First: " + nums[0]
show "Last: " + nums[5]
show "Slice [1:4]: " + nums[1:4]
nums.sort()
show "Sorted: " + nums
nums.reverse()
show "Reversed: " + nums`,
        dict_ops: `person = {"name": "Alice", "age": 30, "city": "NYC"}
show "Name: " + person["name"]
show "Age: " + person["age"]
show "Keys: " + keys(person)
show "Values: " + values(person)
show "Has city? " + has(person, "city")
person["job"] = "Engineer"
show "Updated: " + person`,
        classes: `blueprint Animal:
    action init(name, sound):
        self.name = name
        self.sound = sound
    end

    action speak():
        show self.name + " says " + self.sound
    end
end

dog = Animal("Rex", "Woof!")
cat = Animal("Whiskers", "Meow!")
dog.speak()
cat.speak()`,
        closures: `func make_counter():
    count = 0
    return lambda():
        count = count + 1
        return count
    end
end

counter = make_counter()
show counter()
show counter()
show counter()

# Higher-order function
func apply(f, x):
    return f(x)
end

double = lambda(x): return x * 2 end
show "Double 5 = " + apply(double, 5)`,
        natural: `# GlowLang natural language syntax
x = 42

when x > 10:
    show "x is big"
eif x > 5:
    show "x is medium"
otherwise:
    show "x is small"
end

# Every loop (natural for-each)
every i in from 1 to 5:
    show "Count: " + i
end

# Repeat loop
repeat 3 times:
    show "Hello!"
end`,
        fstrings: `name = "GlowLang"
version = 1
show f"Welcome to {name} v{version}!"

text = "Hello, World!"
show f"First 5: {text[0:5]}"
show f"Last 6: {text[7:]}"
show f"Upper: {upper(text)}"
show f"Length: {len(text)}"`,
        listcomp: `# List comprehensions
squares = [x * x for each x in [1, 2, 3, 4, 5]]
show "Squares: " + squares

evens = [x for each x in [1, 2, 3, 4, 5, 6, 7, 8] when x % 2 == 0]
show "Evens: " + evens

words = ["hello", "world", "glow"]
uppers = [upper(w) for each w in words]
show "Upper: " + uppers`,
        trycatch: `func divide(a, b):
    when b == 0:
        throw "Division by zero!"
    end
    return a / b
end

try:
    show divide(10, 2)
    show divide(10, 0)
catch e:
    show "Caught error: " + e
end

show "Program continues after error handling."`,
        spawn: `func worker(name, n):
    i = 0
    while i < n:
        show "Thread " + name + ": " + i
        i = i + 1
    end
end

spawn worker("A", 3)
spawn worker("B", 3)
show "Main thread continues..."
os.sleep(500)
show "Done."`
    };

    const editor = document.getElementById('code-editor');
    const output = document.getElementById('output');
    const lineNumbers = document.getElementById('line-numbers');
    const runBtn = document.getElementById('run-btn');
    const clearBtn = document.getElementById('clear-btn');
    const examplesSelect = document.getElementById('examples-select');
    const themeToggle = document.getElementById('theme-toggle');
    const status = document.getElementById('status');

    let glowModule = null;

    // ---- Line numbers ----
    function updateLineNumbers() {
        const lines = editor.value.split('\n').length;
        let html = '';
        for (let i = 1; i <= lines; i++) html += i + '\n';
        lineNumbers.textContent = html;
    }

    editor.addEventListener('input', updateLineNumbers);
    editor.addEventListener('scroll', () => {
        lineNumbers.scrollTop = editor.scrollTop;
    });
    updateLineNumbers();

    // ---- Tab key support ----
    editor.addEventListener('keydown', (e) => {
        if (e.key === 'Tab') {
            e.preventDefault();
            const start = editor.selectionStart;
            const end = editor.selectionEnd;
            editor.value = editor.value.substring(0, start) + '    ' + editor.value.substring(end);
            editor.selectionStart = editor.selectionEnd = start + 4;
            updateLineNumbers();
        }
    });

    // ---- Run code ----
    function appendOutput(text, cls) {
        const span = document.createElement('span');
        if (cls) span.className = cls;
        span.textContent = text;
        output.appendChild(span);
        output.scrollTop = output.scrollHeight;
    }

    async function runCode() {
        const code = editor.value;
        if (!code.trim()) return;

        output.textContent = '';
        status.textContent = 'Running…';
        runBtn.disabled = true;

        // Save to localStorage
        try { localStorage.setItem('glow_code', code); } catch (e) { /* ignore */ }

        if (glowModule && glowModule._glow_main) {
            try {
                glowModule.ccall('glow_main', 'number', ['number', 'number'], [0, 0]);
            } catch (err) {
                appendOutput('Runtime error: ' + err.message + '\n', 'output-error');
            }
        } else {
            // Fallback: simulate output for demo
            appendOutput('⚡ WASM module not loaded — showing simulated output\n\n', 'output-info');
            const lines = code.split('\n');
            for (const line of lines) {
                const trimmed = line.trim();
                const showMatch = trimmed.match(/^show\s+"(.*)"\s*$/);
                if (showMatch) {
                    appendOutput(showMatch[1] + '\n', '');
                } else if (trimmed.match(/^show\s+/)) {
                    appendOutput('[expression result]\n', '');
                }
            }
            appendOutput('\n✓ Execution completed (simulated)\n', 'output-info');
            appendOutput('  Build with `make wasm` for full WASM execution.\n', 'output-info');
        }

        status.textContent = 'Ready';
        runBtn.disabled = false;
    }

    runBtn.addEventListener('click', runCode);

    // ---- Keyboard shortcut ----
    document.addEventListener('keydown', (e) => {
        if ((e.ctrlKey || e.metaKey) && e.key === 'Enter') {
            e.preventDefault();
            runCode();
        }
    });

    // ---- Clear ----
    clearBtn.addEventListener('click', () => {
        output.textContent = '';
    });

    // ---- Examples ----
    examplesSelect.addEventListener('change', () => {
        const key = examplesSelect.value;
        if (key && EXAMPLES[key]) {
            editor.value = EXAMPLES[key];
            updateLineNumbers();
            examplesSelect.value = '';
        }
    });

    // ---- Theme toggle ----
    themeToggle.addEventListener('click', () => {
        const current = document.documentElement.getAttribute('data-theme');
        document.documentElement.setAttribute('data-theme', current === 'light' ? '' : 'light');
        themeToggle.textContent = current === 'light' ? '◑' : '◐';
    });

    // ---- Restore saved code ----
    try {
        const saved = localStorage.getItem('glow_code');
        if (saved) {
            editor.value = saved;
            updateLineNumbers();
        }
    } catch (e) { /* ignore */ }

    // ---- Load WASM ----
    async function loadWasm() {
        status.textContent = 'Loading WASM…';
        try {
            const script = document.createElement('script');
            script.src = '../../bin/glowlang.js';
            script.onload = () => {
                if (typeof Module !== 'undefined') {
                    glowModule = Module;
                    status.textContent = 'WASM loaded — Ready';
                }
            };
            script.onerror = () => {
                status.textContent = 'Ready (WASM not available — run `make wasm` to build)';
            };
            document.head.appendChild(script);
        } catch (e) {
            status.textContent = 'Ready (WASM load failed)';
        }
    }

    loadWasm();

    // ---- Gutter resize ----
    const gutter = document.getElementById('gutter');
    let isResizing = false;
    gutter.addEventListener('mousedown', (e) => {
        isResizing = true;
        document.body.style.cursor = 'col-resize';
        document.body.style.userSelect = 'none';
    });
    document.addEventListener('mousemove', (e) => {
        if (!isResizing) return;
        const workspace = document.getElementById('workspace');
        const rect = workspace.getBoundingClientRect();
        const pct = ((e.clientX - rect.left) / rect.width) * 100;
        const editorPanel = document.getElementById('editor-panel');
        const outputPanel = document.getElementById('output-panel');
        editorPanel.style.flex = 'none';
        editorPanel.style.width = pct + '%';
        outputPanel.style.flex = '1';
    });
    document.addEventListener('mouseup', () => {
        isResizing = false;
        document.body.style.cursor = '';
        document.body.style.userSelect = '';
    });
})();
