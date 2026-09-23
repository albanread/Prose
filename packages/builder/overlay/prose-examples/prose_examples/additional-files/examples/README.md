# Examples

Small programs to read, build and change, on a machine that can compile
them. Every one is short enough to take in at a sitting, and each shows one
thing.

| | |
|---|---|
| `01-hello` | A C program, and how to compile one |
| `02-hello-c++` | C++ and the standard library |
| `03-window` | A window on screen: BApplication and BWindow |
| `04-drawing` | A view that paints itself |
| `05-attributes` | Named values beside a file's contents, which the file system indexes |
| `06-threads` | Threads, and the lock that keeps them honest |
| `07-game` | A game pane: palette indices, a sprite, and a filter of your own |

## Building them

These live in `/boot/system/data/prose-examples`, which is read-only, so
take a copy first:

```sh
cp -r /boot/system/data/prose-examples ~/examples
cd ~/examples
make
```

`make` on its own builds every example; `make clean` removes what it built.
Each folder builds on its own the same way, and its first lines say what to
type if you would rather not use `make` at all:

```sh
cd ~/examples/01-hello
clang -O2 -Wall -o hello hello.c
./hello
```

The four that draw or open a window need `-lbe`, the library the Be API
lives in. Nothing else needs a library named on the command line: the C and
C++ standard libraries come in by themselves.

## In the editor

Sisong (Deskbar, Development) opens any of these and compiles the file being
edited on its own: **Run > Compile This File**, or **Compile and Run This
File**, which puts the program's output in the pane under the text. A
compiler's errors are lines you can click.

## What is doing the compiling

`clang` and `clang++`, with `lld` linking. `clang --version` says which. They
compile against Haiku's own headers in `/boot/system/develop/headers` and
link against the libraries in `/boot/system/develop/lib`; `man` pages for the
Be API are not installed, but the headers are worth reading and say more than
most documentation would.
