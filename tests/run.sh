#!/usr/bin/env bash
# tests/run.sh — shell-c regression suite
#
# Usage:
#   make test            build then run (from repo root)
#   bash tests/run.sh    run directly (./shell-c must already be built)
#
# Exit status: 0 if all tests pass, non-zero if any fail.

set -uo pipefail
cd "$(dirname "$0")/.."

SHELL_C="${SHELL_C:-./shell-c}"
TMP="$(mktemp -d)"
PASS=0
FAIL=0

cleanup() { rm -rf "$TMP"; }
trap cleanup EXIT

# ── Colour output (disabled when stdout is not a terminal) ────────────
if [ -t 1 ]; then
    GRN='\033[0;32m' RED='\033[0;31m' BLD='\033[1m' RST='\033[0m'
else
    GRN='' RED='' BLD='' RST=''
fi

# ── Primitives ────────────────────────────────────────────────────────

section() { printf "\n${BLD}%s${RST}\n" "$1"; }

# check NAME GOT WANT
check() {
    local name="$1" got="$2" want="$3"
    if [ "$got" = "$want" ]; then
        printf "  ${GRN}pass${RST}  %s\n" "$name"
        PASS=$((PASS+1))
    else
        printf "  ${RED}FAIL${RST}  %s\n" "$name"
        printf "        want: %s\n" "$(printf '%s' "$want" | cat -v)"
        printf "        got:  %s\n" "$(printf '%s' "$got"  | cat -v)"
        FAIL=$((FAIL+1))
    fi
}

# check_exit NAME INPUT WANT_CODE
check_exit() {
    local name="$1" input="$2" want="$3" got
    printf '%s\n' "$input" | "$SHELL_C" 2>/dev/null && got=0 || got=$?
    if [ "$got" = "$want" ]; then
        printf "  ${GRN}pass${RST}  %s\n" "$name"
        PASS=$((PASS+1))
    else
        printf "  ${RED}FAIL${RST}  %s\n" "$name"
        printf "        want exit %s, got %s\n" "$want" "$got"
        FAIL=$((FAIL+1))
    fi
}

# run LINE... — pipe lines to shell-c, capture stdout only
run()   { printf '%s\n' "$@" | "$SHELL_C" 2>/dev/null; }

# run_f FILE — run a script file through shell-c, capture stdout only
run_f() { "$SHELL_C" "$1" 2>/dev/null; }

# ── Guard ─────────────────────────────────────────────────────────────
if [ ! -x "$SHELL_C" ]; then
    printf "${RED}error:${RST} %s not found or not executable — run 'make' first\n" \
        "$SHELL_C" >&2
    exit 1
fi

printf "${BLD}shell-c test suite${RST}  (%s)\n" "$SHELL_C"

# ═══════════════════════════════════════════════════════════════════════
section "Basic execution"

check "echo"             "$(run 'echo hello')"        "hello"
check "multiple args"    "$(run 'echo foo bar baz')"  "foo bar baz"
check "sequential cmds"  "$(run 'echo a' 'echo b')"  $'a\nb'
check "empty lines skip" "$(run '' 'echo ok' '')"     "ok"

# ═══════════════════════════════════════════════════════════════════════
section "Pipelines"

check "single pipe"   "$(run 'echo hello | cat')"              "hello"
check "tr uppercase"  "$(run 'echo hello | tr a-z A-Z')"       "HELLO"
check "three stages"  "$(run 'echo hello | tr a-z A-Z | rev')" "OLLEH"

# Write fixture files — avoids quoting issues inside shell-c commands
printf 'foo\nbar\nbaz\n' > "$TMP/lines.txt"
printf 'c\na\nb\n'       > "$TMP/unsorted.txt"

check "pipeline grep" "$(run "cat $TMP/lines.txt | grep ^b")"    $'bar\nbaz'
check "pipeline sort" "$(run "cat $TMP/unsorted.txt | sort")"    $'a\nb\nc'

# ═══════════════════════════════════════════════════════════════════════
section "I/O redirection"

OUT="$TMP/out.txt"
ERR="$TMP/err.txt"
IN="$TMP/in.txt"

run "echo hello > $OUT"
check "> creates file"    "$(cat "$OUT")"  "hello"

run "echo world >> $OUT"
check ">> appends"        "$(cat "$OUT")"  $'hello\nworld'

printf 'from file\n' > "$IN"
check "< stdin redirect"  "$(run "cat < $IN")"  "from file"

run "ls /no_such_xyzzy_abc 2>$ERR"
check "2> captures stderr"  "$(test -s "$ERR" && echo yes)"  "yes"

run "ls /no_such_xyzzy_abc 2>>$ERR"
check "2>> appends stderr"  "$(grep -c . "$ERR")"  "2"

BOTH="$TMP/both.txt"
run "ls /no_such_xyzzy_abc > $BOTH 2>&1"
check "2>&1 merges to file"  "$(test -s "$BOTH" && echo yes)"  "yes"

# ═══════════════════════════════════════════════════════════════════════
section "Command substitution"

check "basic \$()"         "$(run 'echo $(echo hello)')"               "hello"
check "pipeline inside"    "$(run 'echo $(echo hello | tr a-z A-Z)')"  "HELLO"
check "mid-token embed"    "$(run 'echo pre$(echo mid)post')"           "premidpost"
check "nested \$()"        "$(run 'echo $(echo $(echo deep))')"         "deep"

YEAR="$(date +%Y)"
check "real cmd capture"   "$(run 'echo $(date +%Y)')"                 "$YEAR"

# \$() inside double-quotes: bash sees \$, passes $() literally to shell-c
check "subst + pipe"  \
    "$(run "echo \$(cat $TMP/lines.txt | grep ^f)")"  "foo"

# ═══════════════════════════════════════════════════════════════════════
section "Variable and tilde expansion"

check "\$VAR"              "$(run 'export FOO=hello' 'echo $FOO')"         "hello"
check "\$VAR mid-token"    "$(run 'export X=world' 'echo hello_$X')"       "hello_world"
check "export overwrites"  "$(run 'export V=a' 'export V=b' 'echo $V')"    "b"
check "\$? after true"     "$(run 'true'  'echo $?')"                       "0"
check "\$? after false"    "$(run 'false' 'echo $?')"                       "1"
check "\$? cmd not found"  "$(run 'no_such_cmd_xyz_abc' 'echo $?')"         "127"
check "\$? pipe last fails" "$(run 'true | false' 'echo $?')"                "1"
check "\$? pipe last wins"  "$(run 'false | true' 'echo $?')"                "0"

check "~ expands to HOME"  "$(run 'echo ~')"       "$HOME"
check "~/path"             "$(run 'echo ~/bin')"   "$HOME/bin"

# ═══════════════════════════════════════════════════════════════════════
section "Built-in commands"

check "cd absolute"       "$(run 'cd /' 'pwd')"                         "/"
check "cd no-arg = HOME"  "$(run 'cd' 'pwd')"                           "$HOME"
check "export + echo"     "$(run 'export MYVAR=42' 'echo $MYVAR')"      "42"
check "cd then cmd"       "$(run 'cd /tmp' 'echo $(pwd)')"              "$(cd /tmp && pwd -P)"
check "cd - returns back"  "$(run 'cd /usr' 'cd /' 'cd -' 'pwd')"        $'/usr\n/usr'
check "cd - no history"    "$(printf 'cd -\n' | "$SHELL_C" 2>&1 >/dev/null)"  "cd: no previous directory"

# ═══════════════════════════════════════════════════════════════════════
section "Script file execution"

cat > "$TMP/basic.sh" << 'SCRIPT'
echo from script
export X=42
echo X is $X
SCRIPT
check "basic script"  \
    "$(run_f "$TMP/basic.sh")"  $'from script\nX is 42'

cat > "$TMP/pipeline.sh" << 'SCRIPT'
echo hello | tr a-z A-Z
echo $(echo nested)
SCRIPT
check "script: pipeline + subst"  \
    "$(run_f "$TMP/pipeline.sh")"  $'HELLO\nnested'

# Use a regular string (no heredoc) so $TMP expands in the script body
printf 'echo redirected > %s/redir_out.txt\ncat %s/redir_out.txt\n' \
    "$TMP" "$TMP" > "$TMP/redir.sh"
check "script: redirection"  \
    "$(run_f "$TMP/redir.sh")"  "redirected"

cat > "$TMP/exitcode.sh" << 'SCRIPT'
exit 7
SCRIPT
"$SHELL_C" "$TMP/exitcode.sh" >/dev/null 2>/dev/null && _ec=0 || _ec=$?
check "script: exit code"  "$_ec"  "7"

# Verify no prompt leaks to stdout in script mode
PROMPT_CHECK="$(run_f "$TMP/basic.sh" | grep -c '\$' || true)"
check "script: no prompt in stdout"  "$PROMPT_CHECK"  "0"

# ═══════════════════════════════════════════════════════════════════════
section "Exit status (shell process)"

check_exit "exit 0"          "exit 0"         "0"
check_exit "exit 1"          "exit 1"         "1"
check_exit "exit 42"         "exit 42"        "42"
check_exit "exit no-arg=\$?" $'false\nexit'  "1"

# ═══════════════════════════════════════════════════════════════════════
section "Background execution"

check "& does not block"  \
    "$(run 'sleep 0.1 &' 'echo not blocked')"  \
    "not blocked"

check "& + foreground sequential"  \
    "$(run 'sleep 0.1 &' 'echo one' 'echo two')"  \
    $'one\ntwo'

# ═══════════════════════════════════════════════════════════════════════
section "Edge cases"

check "blank lines ignored"    "$(run '' '' 'echo ok' '')"       "ok"
check "multi-level subst"      "$(run 'echo $(echo $(echo hi))')" "hi"
check "empty pipeline segment" "$(run 'echo hi | cat')"           "hi"

# Unknown command: shell-c should print an error to stderr (not stdout)
# and last_exit should be 127
check "unknown cmd \$?"  "$(run 'cmd_does_not_exist_xyz' 'echo $?')"  "127"

# Subst captures multi-line output (newlines stripped except trailing)
printf 'line1\nline2\nline3\n' > "$TMP/multi.txt"
check "subst strips trailing newline"  \
    "$(run "echo \$(cat $TMP/multi.txt | head -1)")"  "line1"

# ═══════════════════════════════════════════════════════════════════════
TOTAL=$((PASS+FAIL))
printf "\n${BLD}Results: ${GRN}%d passed${RST}${BLD}, ${RED}%d failed${RST}${BLD}, %d total${RST}\n" \
    "$PASS" "$FAIL" "$TOTAL"
printf "\n"

[ "$FAIL" -eq 0 ]
