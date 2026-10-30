#!/usr/bin/env bash
# Run the GlowLang test suite on Linux and macOS (also works in Git Bash).
#
# Same checks and the same expected files as scripts/run-tests.ps1:
#   - exit code: 0, or the code listed in tests/expected/exit_codes.txt;
#   - stdout: must equal tests/expected/<name>.out, where {{ANY}} in an
#     expected line matches any text (timings, memory figures);
#   - stderr: every line of tests/expected/<name>.err must appear in it.
# tests/expected/modes.txt marks tests "unordered" (lines compared as a sorted
# multiset, for threads that interleave) or "skip-stdout".
#
# Usage: scripts/run-tests.sh [name-glob]        e.g. scripts/run-tests.sh 'regress_*'
# Environment: GLOWLANG_BIN (default ./bin/glowlang), TEST_TIMEOUT (seconds, default 60).
set -u
root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"
bin="${GLOWLANG_BIN:-./bin/glowlang}"
timeout_s="${TEST_TIMEOUT:-60}"
filter="${1:-*}"
exp="tests/expected"

if [ ! -x "$bin" ]; then
    echo "GlowLang binary not found: $bin (build with 'make' or set GLOWLANG_BIN)" >&2
    exit 2
fi

# macOS has no `timeout` unless coreutils is installed (as gtimeout).
if command -v timeout >/dev/null 2>&1; then
    run_limited() { timeout "$timeout_s" "$@"; }
elif command -v gtimeout >/dev/null 2>&1; then
    run_limited() { gtimeout "$timeout_s" "$@"; }
else
    echo "note: no timeout command found; tests run without a time limit" >&2
    run_limited() { "$@"; }
fi

# Split $2 into lines in the array named $1 (bash 3.2 has no mapfile).
read_lines() {
    local __name="$1" __line
    eval "$__name=()"
    while IFS= read -r __line; do eval "$__name+=(\"\$__line\")"; done <<<"$2"
}

# Value for key $2 in a "name value" table file $1 (comment lines start with #).
lookup() {
    [ -f "$1" ] || return 0
    awk -v k="$2" '$1 !~ /^#/ && $1 == k { print $2; exit }' "$1"
}

# CRLF -> LF and backslashes -> forward slashes, like the PowerShell runner.
normalize() { tr -d '\r' | sed 's#\\#/#g'; }

glob_escape() { printf '%s' "$1" | sed 's/[][*?\\]/\\&/g'; }

# Does output line $2 match expected line $1 ({{ANY}} matches anything)?
line_matches() {
    local want="$1" got="$2"
    if [[ "$want" != *"{{ANY}}"* ]]; then
        [[ "$want" == "$got" ]]
        return
    fi
    local pat="" rest="$want" part
    while [[ "$rest" == *"{{ANY}}"* ]]; do
        part="${rest%%"{{ANY}}"*}"
        rest="${rest#*"{{ANY}}"}"
        pat+="$(glob_escape "$part")*"
    done
    pat+="$(glob_escape "$rest")"
    [[ "$got" == $pat ]]
}

pass=0
fail=0
failures=()
start=$(date +%s)
out_f=$(mktemp)
err_f=$(mktemp)
trap 'rm -f "$out_f" "$err_f"' EXIT

for t in tests/$filter.glow; do
    [ -f "$t" ] || continue
    name="$(basename "$t")"
    base="${name%.glow}"
    run_limited "$bin" run "$t" </dev/null >"$out_f" 2>"$err_f"
    code=$?
    [ "$code" -eq 124 ] && code=TIMEOUT

    want_code="$(lookup "$exp/exit_codes.txt" "$name")"
    want_code="${want_code:-0}"
    mode="$(lookup "$exp/modes.txt" "$name")"
    out="$(normalize <"$out_f")"
    err="$(normalize <"$err_f")"
    problems=()
    [ "$code" != "$want_code" ] && problems+=("exit code $code (expected $want_code)")

    if [ "$mode" != "skip-stdout" ]; then
        if [ -f "$exp/$base.out" ]; then
            want="$(normalize <"$exp/$base.out")"
            if [ "$mode" = "unordered" ]; then
                want="$(printf '%s\n' "$want" | LC_ALL=C sort)"
                out="$(printf '%s\n' "$out" | LC_ALL=C sort)"
            fi
            read_lines wl "$want"
            read_lines ol "$out"
            n=${#wl[@]}
            [ ${#ol[@]} -gt "$n" ] && n=${#ol[@]}
            for ((i = 0; i < n; i++)); do
                if [ "$i" -ge ${#wl[@]} ] || [ "$i" -ge ${#ol[@]} ] || ! line_matches "${wl[$i]}" "${ol[$i]}"; then
                    w="${wl[$i]-<end of output>}"
                    g="${ol[$i]-<end of output>}"
                    problems+=("stdout differs at line $((i + 1)): expected '$w' got '$g'")
                    break
                fi
            done
        else
            problems+=("no expected output file ($exp/$base.out)")
        fi
    fi

    if [ -f "$exp/$base.err" ]; then
        while IFS= read -r need; do
            [ -n "$need" ] || continue
            [[ "$err" == *"$need"* ]] || problems+=("stderr lacks '$need'")
        done < <(normalize <"$exp/$base.err")
    fi

    if [ ${#problems[@]} -eq 0 ]; then
        pass=$((pass + 1))
    else
        fail=$((fail + 1))
        # The first line (the error message) and the last three (where).
        first_err="$(printf '%s\n' "$err" | sed 's/\x1b\[[0-9;]*m//g' | grep -m1 -v '^[[:space:]]*$')"
        tail_err="$( { printf '%s\n' "$first_err"; printf '%s\n' "$err" | tail -n 3; } | paste -sd'|' -)"
        msg="FAIL $name: $(printf '%s; ' "${problems[@]}")"
        [ -n "$tail_err" ] && msg+="  [stderr: $tail_err]"
        failures+=("$msg")
    fi
done

for f in "${failures[@]+"${failures[@]}"}"; do echo "$f"; done
echo "Total: $((pass + fail))  Passed: $pass  Failed: $fail  ($(($(date +%s) - start))s)"
[ "$fail" -eq 0 ]
