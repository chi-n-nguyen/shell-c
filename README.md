# shell-c

A POSIX-compliant Unix shell implemented in C.

## Features

- **Pipelines** — arbitrary-length pipelines (`cmd1 | cmd2 | cmd3`)
- **I/O redirection** — `<`, `>`, `>>`
- **Background execution** — trailing `&`
- **Signal handling** — Ctrl+C and Ctrl+Z reach the foreground pipeline, not the shell; background children are reaped asynchronously via `SIGCHLD`
- **Process groups** — each pipeline runs in its own process group so signals reach every stage
- **Job control** — `jobs`, `fg [%N]`, `bg [%N]`; Ctrl+Z suspends a foreground job and registers it in the job table; `fg` restores terminal ownership and resumes via `SIGCONT`
- **Environment variable expansion** — `$VAR` and `$?` are expanded at parse time
- **Built-ins** — `cd`, `exit`, `export`, `history`, `jobs`, `fg`, `bg`
- **Command history** — ring buffer of the last 100 commands
- **Trace mode** — `--trace` logs every fork, exec, and pipe to stderr

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
