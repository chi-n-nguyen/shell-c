# shell-c

A POSIX-compliant Unix shell implemented in C with pipelines, job control, and async-signal-safe process management.

## Features

- **Multi-stage pipelines** — arbitrary-length pipelines (`cmd1 | cmd2 | cmd3`)
- **I/O redirection** — `<`, `>`, `>>`, `2>`, `2>>`, `2>&1`
- **Background execution** — trailing `&`
- **Job control** — `jobs`, `fg [%N]`, `bg [%N]`; Ctrl+Z suspends a foreground job and registers it in the job table; `fg` restores terminal ownership via `tcsetpgrp` and resumes the process group with `SIGCONT`
- **Signal handling** — Ctrl+C and Ctrl+Z reach the foreground pipeline, not the shell; `SIGCHLD` is blocked around fork loops and job table mutations to eliminate handler races
- **Process groups** — each pipeline runs in its own process group so signals reach every stage
- **Command substitution** — `$(cmd)` forks a subshell, captures its stdout, and splices the result inline; supports embedded substitutions (`/usr/$(uname -m)/lib`) and pipelines inside (`$(cat f | wc -l)`)
- **Variable expansion** — `$VAR` looks up the environment; `$?` expands to the last foreground exit status
- **Built-ins** — `cd`, `exit`, `export`, `history`, `jobs`, `fg`, `bg`
- **Command history** — ring buffer of the last 100 commands
- **Trace mode** — `--trace` logs every fork, exec, and pipe decision to stderr

## Build

```
make
```

Requires a C11 compiler and POSIX.1-2008.

## Usage

```
./shell-c           # interactive mode
./shell-c --trace   # with execution tracing
```

## Examples

```sh
# Multi-stage pipeline with I/O redirection
cat /etc/passwd | grep root | cut -d: -f1 > out.txt

# Stderr redirection
gcc bad.c 2> errors.txt          # stderr to file
make 2>&1 | grep error           # merge stderr into stdout, then pipe
make > build.log 2>&1            # stdout + stderr to same file
make >> build.log 2>> build.log  # append both streams

# Background execution and job control
sleep 60 &          # [1] 12345
jobs                # [1] running   sleep 60 &
fg %1               # brings job 1 to foreground
# Ctrl+Z            # [1]+ stopped   sleep 60 &
bg %1               # resumes in background

# Command substitution — standalone, embedded, pipeline inside
echo "today is $(date)"
echo /usr/$(uname -m)/lib
echo "$(cat /etc/hosts | wc -l) lines"

# Exit status and variable expansion
ls /nonexistent
echo $?             # 1
echo $HOME          # /Users/you
```

## Built-in commands

| Command | Description |
|---------|-------------|
| `cd [dir]` | Change directory; defaults to `$HOME` |
| `exit [code]` | Exit with optional status code |
| `export VAR=VALUE` | Set an environment variable |
| `history` | Print command history |
| `jobs` | List active background and stopped jobs |
| `fg [%N]` | Bring job N (or the most recent) to the foreground |
| `bg [%N]` | Resume stopped job N (or most recent) in the background |

## Implementation notes

| Concern | Approach |
|---------|----------|
| Pipeline wiring | `pipe(2)` + `dup2(2)` in each child before `execvp`; all unused pipe ends closed in parent and child |
| Stderr redirect | stdout redirected before stderr in `apply_redirections` so `> file 2>&1` correctly points both fds at the file; `2>&1` is `dup2(STDOUT_FILENO, STDERR_FILENO)` |
| Command substitution | `$(cmd)` forks a subshell whose stdout is a pipe; parent drains the pipe with a doubling read loop and strips trailing newlines; stdin set to `/dev/null` suppresses `tcsetpgrp` inside the subshell; `SIGCHLD` blocked around fork+`waitpid` prevents the reaping handler racing with `cmd_subst`; expanded strings live in a 64 KB arena reset once per command line |
| Depth-aware tokenizer | `next_arg()` and `split_pipes()` track `$(` nesting depth so spaces and `\|` inside a substitution are not treated as delimiters |
| Process isolation | `setpgid(2)` puts every pipeline in its own process group; both parent and child call it to close the TOCTOU race |
| Terminal handoff | `tcsetpgrp(3)` gives the terminal to the foreground pipeline; reclaimed by the shell on return |
| Job table safety | `SIGCHLD` blocked (`sigprocmask`) around `fork` loops and `job_add`/`job_remove`; handler writes only `volatile int` fields — no stdio |
| Stopped job detection | `SA_NOCLDSTOP` omitted so `SIGCHLD` fires on `SIGTSTP`; `waitpid` called with `WUNTRACED` in the foreground wait path |
| Background reaping | `waitpid(-1, WNOHANG \| WUNTRACED)` in `SIGCHLD` handler updates job status; main loop calls `jobs_notify_done()` before each prompt |
