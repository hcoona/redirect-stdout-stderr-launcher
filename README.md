# Redirect Launcher

This is a tool help to redirect STDOUT & STDERR to rolling files with size limitation.

## Getting Started

### Usage

```bash
redirect_launcher $PWD/stdout.txt $PWD/stderr.txt <your program with args>
```

### Compile

```bash
python bazelisk.py //...
```

## TODO list

1. Read configuration file.
1. Add `port.h` for posix functions on Windows.
