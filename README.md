# Final Project: Resource Package

Unzip this package and use the folder as your **project folder**: write your `.c` and `.h` files next to these files.

| Item | What it is |
| --- | --- |
| `assignment.pdf` | The assignment. Read it first |
| `Makefile` | Builds your program. List your files in it |
| `checker.c` | Runs your program on the public tests |
| `tests/P01` to `tests/P10` | The 10 public tests |

`checker.c` is a separate tool with its own `main`. Do not list it in the Makefile or add it to an IDE project; otherwise the build fails with `multiple definition of 'main'`.

## Quick start

Open a terminal in the project folder (VS Code: *Terminal > New Terminal*), then:

1. **Build your program.** List your `.c` and `.h` files in `Makefile`, then run `mingw32-make` (Windows) or `make` (macOS/Linux). This creates `inventory.exe` (macOS/Linux: `inventory`).
2. **Build the checker once:** `gcc checker.c -o checker`
3. **Run the checker:** `./checker`

After each change to your code, repeat steps 1 and 3.

## Building with make

Edit only these two lines of `Makefile`:

```make
SRCS   = main.c store.c      # all .c files of your program (not checker.c)
HDRS   = store.h             # all your .h files
```

Keep the other lines unchanged: the TA team builds every project on Linux with this Makefile. The line under `inventory:` must start with a TAB; if it starts with spaces, make stops with `missing separator`.

**Windows: getting `mingw32-make`.** It comes with MinGW, together with `gcc`. Check with `gcc --version` and `mingw32-make --version`. If it is missing:

- **Code::Blocks:** add `C:\Program Files\CodeBlocks\MinGW\bin` to `Path` (search for "Edit the system environment variables" > *Environment Variables* > `Path` > *New*), then open a new terminal.
- **MSYS2:** run `pacman -S mingw-w64-ucrt-x86_64-make` in the MSYS2 terminal.

You can write and debug your code in any editor, but build it with make: the VS Code *Run* button and Code Runner compile only the open file.

## Running the checker

`./checker` runs the 10 public tests on `inventory.exe` (macOS/Linux: `inventory`) in the project folder and prints `PASS` or `FAIL` for each. It needs no options. Always start it from a terminal; double-clicking `checker.exe` does not run the tests.

A failing test shows the first difference and the command in `input.txt` that printed it:

```text
FAIL P05
  Standard output: first difference at line 33, printed by input line 19: CHECKOUT C3
          31 | RECEIPT C3
          32 |   A x2 @100 = 200
    Expected:
          33 |   B x1 @3 *50% = 2
          34 | TOTAL 202
          35 | COST 80
    Your output:
          33 |   B x1 @3 *50% = 1
          34 | TOTAL 201
          35 | COST 80
  Your output and saved files are kept in: ...\runs\P05
```

The checker also tells you when the only difference is spaces (shown as `.`), a line is missing its newline, your program stops early, prints extra lines or a prompt, crashes, or `main` does not return 0. Line endings (Windows CRLF or LF) are ignored; everything else must match exactly.

Each test runs in its own folder `runs/<test>`. Your output (`actual_output.txt`) and the files your program saved stay there, so you can compare them with `tests/<test>/expected_output.txt` and `tests/<test>/expected_files/`.

On Windows there is no time limit: if the checker stays on one test, your program is probably in an endless loop; press Ctrl+C. On macOS and Linux a test is stopped after 10 seconds.

## Suggested order

Build the program in steps. After each step, the listed tests should pass.

| Step | Implement | Tests |
| --- | --- | --- |
| 1 | IMPORT, PRICE, QUERY, REPORT, QUIT | P02 |
| 2 | CART, ADD, CHECKOUT with FIFO (no gifts yet: print `GIFT none`) | P03, P04, P10 |
| 3 | PROMO | P05 |
| 4 | Gifts | P06 |
| 5 | SAVE, LOAD | P01, P07, P08 |
| 6 | Every error message | P09 |

You can add error handling along the way; P09 also checks file errors, so it passes only at the end.

The public tests show every output format and the basic rules. The hidden tests used for grading also check boundaries, combinations of rules and errors in detail, so test your own cases too.

## Before you submit

- [ ] In a fresh copy of your repository, `make` builds `inventory` without errors, and there is no `implicit declaration` warning.
- [ ] The Makefile lists every `.c` and `.h` file of your program (not `checker.c`) and keeps the template's other lines.
- [ ] `./checker` passes the public tests.
- [ ] Every `#include "..."` matches the real file name exactly, including upper and lower case: Linux does not find `store.h` from `#include "Store.h"`.
- [ ] Only standard C: no `conio.h`, `windows.h`, `system("pause")`, `getch`, `itoa`, `fflush(stdin)`, `strdup`, `strtok_r`, `strcasecmp`, `stricmp` or `strlwr`. These may work on Windows but fail on Linux.
- [ ] The program prints only the required output: no menus, prompts or debug messages.
- [ ] `main` returns 0 after `QUIT`.

You do not need to commit `checker.c`, `tests/`, `runs/`, `checker.exe` or `inventory.exe`.
