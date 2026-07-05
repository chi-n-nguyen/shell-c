# shell-c

A Unix shell written in C.

## Features

- **Pipelines**: arbitrary-length `cmd1 | cmd2 | cmd3`
- **I/O redirection**: `<`, `>`, `>>`, `2>`, `2>>`, `2>&1`
- **Command substitution**: `$(cmd)` runs in a subshell and splices stdout inline; works mid-token (`/usr/$(uname -m)/lib`) and with pipelines inside (`$(cat f | wc -l)`)
- **Expansion**: `$VAR`, `$?`, `~`, `~user`
- **Job control**: `jobs`, `fg [%N]`, `bg [%N]`; Ctrl+Z stops a foreground job, `fg` restores terminal ownership and resumes with `SIGCONT`
- **Background execution**: trailing `&`
- **Signal handling**: Ctrl+C and Ctrl+Z reach the foreground pipeline, not the shell; `SIGCHLD` is blocked around fork loops to eliminate handler races
- **Script execution**: `./shell-c script.sh` runs non-interactively; prompts and history are suppressed automatically
- **Persistent history**: saved to `~/.shell-c_history` on exit, restored on startup, capped at 100 entries
- **Built-ins**: `cd`, `exit`, `export`, `history`, `jobs`, `fg`, `bg`
- **Trace mode**: `--trace` logs every fork, exec, and pipe decision to stderr

## Build

```
make
make test   # run the regression suite (48 tests)
```

Requires a C11 compiler and POSIX.1-2008.

## Usage

```
./shell-c                    # interactive
./shell-c script.sh          # run a script file
./shell-c --trace            # interactive with execution tracing
./shell-c --trace script.sh  # trace a script
```

## Examples

```sh
# Multi-stage pipeline
cat /etc/passwd | grep root | cut -d: -f1 > out.txt

# Stderr redirection
gcc bad.c 2> errors.txt       # stderr to file
make 2>&1 | grep error        # merge stderr into stdout then pipe
make > build.log 2>&1         # stdout and stderr to the same file

# Job control
sleep 60 &    # [1] 12345 — runs in background
jobs          # [1] running   sleep 60 &
fg %1         # bring to foreground
              # Ctrl+Z to stop it
bg %1         # resume in background

# Command substitution
echo "today is $(date)"
echo /usr/$(uname -m)/lib
echo "$(cat /etc/hosts | wc -l) lines"

# Variable and tilde expansion
ls /nonexistent
echo $?        # 1
echo ~         # /Users/you
echo $HOME     # /Users/you
```

## Built-ins

| Command | Description |
|---------|-------------|
| `cd [dir]` | Change directory; defaults to `$HOME`; `cd -` returns to the previous directory |
| `exit [code]` | Exit with optional status code |
| `export VAR=VALUE` | Set an environment variable |
| `history` | Print command history |
| `jobs` | List active background and stopped jobs |
| `fg [%N]` | Bring job N to the foreground |
| `bg [%N]` | Resume stopped job N in the background |

## Implementation notes

| Concern | Approach |
|---------|----------|
| Pipeline wiring | `pipe(2)` + `dup2(2)` per child before `execvp`; all unused ends closed in parent and child |
| Stderr redirect | stdout is redirected before stderr so `> file 2>&1` correctly points both fds at the file |
| Command substitution | subshell stdout captured via pipe; parent drains with a doubling read loop; stdin set to `/dev/null` to suppress terminal handoff inside the subshell; `SIGCHLD` blocked around fork and `waitpid` to prevent the reaping handler stealing the exit status |
| Depth-aware tokenizer | `next_arg()` and `split_pipes()` track `$(` nesting depth so spaces and `|` inside a substitution are not treated as delimiters |
| Process isolation | `setpgid(2)` puts every pipeline in its own process group; both parent and child call it to close the TOCTOU race |
| Terminal handoff | `tcsetpgrp(3)` transfers the terminal to the foreground pipeline; reclaimed by the shell on return |
| Job table safety | `SIGCHLD` blocked via `sigprocmask` around `fork` loops and `job_add`/`job_remove`; handler writes only `volatile int` fields, no stdio |
| Stopped job detection | `SA_NOCLDSTOP` omitted so `SIGCHLD` fires on stop; `waitpid` uses `WUNTRACED` in the foreground wait path |
| Background reaping | `waitpid(-1, WNOHANG \| WUNTRACED)` in `SIGCHLD` handler updates job status; main loop notifies before each prompt |
| Persistent history | loaded in `history_init`; rewritten on exit capped at 100 lines; gated on `isatty(fileno(src))` so script runs don't pollute the file |
| Script mode | `shell_loop` takes `FILE *src`; `isatty(fileno(src))` controls prompts, history, and job notifications with no extra flags |
