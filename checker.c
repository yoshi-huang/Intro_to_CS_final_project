/*
 * checker.c - test checker for the Final Project (store inventory system).
 *
 * Keep this file in your project folder, next to your Makefile and the
 * "tests" folder. It is NOT part of your program: do not list it in SRCS of
 * the Makefile and do not add it to an IDE project (it has its own main).
 *
 * Build your program with make first, then build the checker once:
 *     gcc checker.c -o checker
 * and run it from a terminal (command line) in your project folder:
 *     ./checker
 * It runs all public tests on inventory(.exe) in this folder. Students need no
 * options; the options in usage() are for the TA team's grading script.
 *
 * For every test folder (tests/P01, tests/P02, ...) the checker
 *   1. creates runs/<test>/ and copies the files that the test's commands use,
 *   2. runs your program there with input.txt as its standard input,
 *   3. compares what your program printed with expected_output.txt, and every
 *      file in expected_files/ with the file your program saved.
 * CRLF and LF line endings are treated as the same; everything else must match
 * exactly. Your output and saved files stay in runs/<test>/ for you to inspect.
 *
 * A test with input_2.txt runs your program a second time in the same folder
 * (to check that a saved file can be loaded by a new run). Its answers are
 * expected_output_2.txt and expected_files_2/.
 */
#ifndef _WIN32
#define _XOPEN_SOURCE 700      /* popen, realpath */
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define popen _popen
#define pclose _pclose
#define SEP '\\'
#define POPEN_MODE "rb"
#else
#include <signal.h>
#include <sys/wait.h>
#define SEP '/'
#define POPEN_MODE "r"
#endif

enum {
    PATH_SIZE = 4096,
    MAX_STAGES = 9,
    MAX_NAMES = 128,
    NAME_SIZE = 256,         /* file names in commands are short */
    MAX_CASES = 200,
    CONTEXT_BEFORE = 2,      /* matching lines shown before a difference */
    LINES_SHOWN = 3,         /* lines shown from the first difference on */
    CPU_LIMIT_SECONDS = 10   /* macOS/Linux only: stops endless loops */
};
#define OUTPUT_LIMIT (8L * 1024 * 1024)

typedef struct {
    int full;      /* --full: print the whole expected and actual output */
    int brief;     /* --brief: one line per problem (for TAs) */
} Options;

/* ---------- small file and path helpers ---------- */

static int is_dir(const char *path)
{
#ifdef _WIN32
    struct _stat st;
    return _stat(path, &st) == 0 && (st.st_mode & _S_IFDIR);
#else
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

static int is_file(const char *path)
{
#ifdef _WIN32
    struct _stat st;
    return _stat(path, &st) == 0 && (st.st_mode & _S_IFREG);
#else
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
#endif
}

static void make_dir(const char *path)
{
#ifdef _WIN32
    _mkdir(path);
#else
    mkdir(path, 0777);
#endif
}

/* Creates a folder and any missing parent folders. */
static void make_dirs(const char *path)
{
    char partial[PATH_SIZE];
    size_t i, n = strlen(path);
    if (n >= PATH_SIZE) return;
    for (i = 1; i <= n; ++i) {
        if (i == n || path[i] == '/' || path[i] == SEP) {
            memcpy(partial, path, i);
            partial[i] = '\0';
            if (!is_dir(partial)) make_dir(partial);
        }
    }
}

static int full_path(const char *path, char out[PATH_SIZE])
{
#ifdef _WIN32
    return _fullpath(out, path, PATH_SIZE) != NULL;
#else
    char *resolved = realpath(path, NULL);
    if (resolved == NULL || strlen(resolved) >= PATH_SIZE) {
        free(resolved);
        return 0;
    }
    strcpy(out, resolved);
    free(resolved);
    return 1;
#endif
}

/* out = dir + separator + name (cut short if it would not fit) */
static void join(char out[PATH_SIZE], const char *dir, const char *name)
{
    size_t d = strlen(dir), n = strlen(name);
    if (d > PATH_SIZE - 2) d = PATH_SIZE - 2;
    if (n > PATH_SIZE - 2 - d) n = PATH_SIZE - 2 - d;
    memmove(out, dir, d);
    out[d] = SEP;
    memcpy(out + d + 1, name, n);
    out[d + 1 + n] = '\0';
}

static void parent_dir(char out[PATH_SIZE], const char *path)
{
    char *cut;
    snprintf(out, PATH_SIZE, "%s", path);
    cut = strrchr(out, SEP);
#ifdef _WIN32
    if (strrchr(out, '/') > cut) cut = strrchr(out, '/');
#endif
    if (cut == NULL) strcpy(out, ".");
    else if (cut == out) cut[1] = '\0';
    else *cut = '\0';
}

typedef struct {
    char *data;
    size_t size;
} Text;

static int append(Text *t, const char *bytes, size_t n)
{
    char *grown = realloc(t->data, t->size + n + 1);
    if (grown == NULL) return 0;
    t->data = grown;
    memcpy(t->data + t->size, bytes, n);
    t->size += n;
    t->data[t->size] = '\0';
    return 1;
}

/* Returns 0 when the file does not exist. */
static int read_file(const char *path, Text *t)
{
    char chunk[8192];
    size_t n;
    FILE *fp = fopen(path, "rb");
    t->data = NULL;
    t->size = 0;
    if (fp == NULL) return 0;
    append(t, "", 0);
    while ((n = fread(chunk, 1, sizeof chunk, fp)) > 0)
        append(t, chunk, n);
    fclose(fp);
    return 1;
}

static int write_file(const char *path, const Text *t)
{
    FILE *fp = fopen(path, "wb");
    if (fp == NULL) return 0;
    if (t->size > 0) fwrite(t->data, 1, t->size, fp);
    fclose(fp);
    return 1;
}

static int copy_file(const char *from, const char *to)
{
    Text t;
    int ok;
    if (!read_file(from, &t)) return 0;
    ok = write_file(to, &t);
    free(t.data);
    return ok;
}

/* CRLF -> LF, so Windows and Linux line endings compare equal. */
static void normalize(Text *t)
{
    size_t from, to = 0;
    for (from = 0; from < t->size; ++from)
        if (!(t->data[from] == '\r' && from + 1 < t->size && t->data[from + 1] == '\n'))
            t->data[to++] = t->data[from];
    t->size = to;
    if (t->data != NULL) t->data[to] = '\0';
}

/* ---------- splitting text into lines ---------- */

typedef struct {
    const char *start;
    size_t len;
} Line;

typedef struct {
    Line *line;
    size_t count;
    int ends_with_newline;
} Lines;

static Lines split_lines(const Text *t)
{
    Lines l = {NULL, 0, 1};
    size_t i, begin = 0;
    for (i = 0; i <= t->size; ++i) {
        if (i == t->size && begin == t->size) break;
        if (i == t->size || t->data[i] == '\n') {
            Line *grown = realloc(l.line, (l.count + 1) * sizeof *grown);
            if (grown == NULL) break;
            l.line = grown;
            l.line[l.count].start = t->data + begin;
            l.line[l.count].len = i - begin;
            l.count++;
            if (i == t->size) l.ends_with_newline = 0;
            begin = i + 1;
        }
    }
    return l;
}

static int same_line(Line a, Line b)
{
    return a.len == b.len && memcmp(a.start, b.start, a.len) == 0;
}

static int starts_with(Line l, const char *prefix)
{
    size_t n = strlen(prefix);
    return l.len >= n && memcmp(l.start, prefix, n) == 0;
}

/* Prints one line; invisible or non-ASCII bytes appear as \t, \r, \xNN. */
static void print_line(Line l, int dots_for_spaces)
{
    size_t i;
    for (i = 0; i < l.len; ++i) {
        unsigned char c = (unsigned char)l.start[i];
        if (c == ' ' && dots_for_spaces) putchar('.');
        else if (c == '\t') fputs("\\t", stdout);
        else if (c == '\r') fputs("\\r", stdout);
        else if (c < 32 || c > 126) printf("\\x%02X", c);
        else putchar(c);
    }
}

static void print_numbered(const Lines *l, size_t from, size_t to, const char *indent)
{
    size_t i;
    for (i = from; i < to && i < l->count; ++i) {
        printf("%s%6lu | ", indent, (unsigned long)(i + 1));
        print_line(l->line[i], 0);
        putchar('\n');
    }
}

/* Lines with all spaces removed are equal: the difference is only spacing. */
static int same_without_spaces(Line a, Line b)
{
    size_t i = 0, j = 0;
    for (;;) {
        while (i < a.len && a.start[i] == ' ') ++i;
        while (j < b.len && b.start[j] == ' ') ++j;
        if (i == a.len || j == b.len) return i == a.len && j == b.len;
        if (a.start[i] != b.start[j]) return 0;
        ++i;
        ++j;
    }
}

/* ---------- the commands of a test ---------- */

typedef struct {
    Text text;
    Lines lines;            /* input lines, used to explain differences */
    char save[MAX_NAMES][NAME_SIZE];
    int save_line[MAX_NAMES];
    int saves;
} Stage;

/* Every output block begins with one of these; the k-th block belongs to the
 * k-th command. Used only to tell students which command printed a line. */
static int starts_block(Line l)
{
    return starts_with(l, "OK ") || starts_with(l, "ERR ") || starts_with(l, "RECEIPT ")
        || starts_with(l, "QUERY ") || (l.len == 6 && starts_with(l, "REPORT"))
        || (l.len == 3 && starts_with(l, "BYE"));
}

/* Returns the input line number (1-based) of the command that printed expected
 * output line `index` (0-based), or 0 when it cannot be determined. */
static int command_line_for(const Lines *expected, size_t index, const Stage *stage, size_t *command)
{
    size_t i, blocks = 0;
    if (expected->count == 0) return 0;
    if (index >= expected->count) index = expected->count - 1;
    for (i = 0; i <= index; ++i)
        if (starts_block(expected->line[i])) blocks++;
    if (blocks == 0) return 0;
    for (i = 0; i < stage->lines.count; ++i) {
        if (stage->lines.line[i].len == 0) continue;
        if (--blocks == 0) {
            *command = i;
            return (int)i + 1;
        }
    }
    return 0;
}

static int safe_name(const char *name)
{
    return name[0] != '\0' && name[0] != '/' && name[0] != '\\' && strchr(name, ':') == NULL
        && strstr(name, "..") == NULL;
}

static void add_name(char names[][NAME_SIZE], int *count, const char *name)
{
    int i;
    if (strlen(name) >= NAME_SIZE) return;
    for (i = 0; i < *count; ++i)
        if (strcmp(names[i], name) == 0) return;
    if (*count < MAX_NAMES) strcpy(names[(*count)++], name);
}

/* Finds the file names used by IMPORT, LOAD and SAVE commands. */
static void scan_commands(Stage *stage, char names[][NAME_SIZE], int *name_count)
{
    size_t i;
    for (i = 0; i < stage->lines.count; ++i) {
        char line[512], command[16], name[PATH_SIZE];
        Line l = stage->lines.line[i];
        size_t n = l.len < sizeof line - 1 ? l.len : sizeof line - 1;
        memcpy(line, l.start, n);
        line[n] = '\0';
        if (sscanf(line, "%15s %4095s", command, name) != 2) continue;
        if (strcmp(command, "IMPORT") && strcmp(command, "LOAD") && strcmp(command, "SAVE")) continue;
        if (!safe_name(name) || strlen(name) >= NAME_SIZE) continue;
        add_name(names, name_count, name);
        if (strcmp(command, "SAVE") == 0 && stage->saves < MAX_NAMES) {
            strcpy(stage->save[stage->saves], name);
            stage->save_line[stage->saves++] = (int)i + 1;
        }
    }
}

/* ---------- running the student's program ---------- */

typedef struct {
    int ok;              /* exited normally with code 0 */
    int too_much;        /* output limit reached */
    int crashed;         /* ended by a crash: buffered output may be lost */
    char problem[256];   /* why it is not ok */
} RunResult;

static void quote_for_shell(char *out, size_t size, const char *path)
{
#ifdef _WIN32
    snprintf(out, size, "\"%s\"", path);
#else
    size_t used = 0;
    const char *p;
    out[used++] = '\'';
    for (p = path; *p != '\0' && used + 5 < size; ++p) {
        if (*p == '\'') {
            memcpy(out + used, "'\\''", 4);
            used += 4;
        } else {
            out[used++] = *p;
        }
    }
    out[used++] = '\'';
    out[used] = '\0';
#endif
}

static RunResult run_program(const char *exe, const char *run_dir, const char *input,
                             const char *errors_name, Text *output)
{
    RunResult r = {0, 0, 0, ""};
    static char command[8 * PATH_SIZE], q_dir[2 * PATH_SIZE], q_exe[2 * PATH_SIZE];
    static char q_in[2 * PATH_SIZE];
    char chunk[8192];
    size_t n;
    int status;
    FILE *pipe;

    quote_for_shell(q_dir, sizeof q_dir, run_dir);
    quote_for_shell(q_exe, sizeof q_exe, exe);
    quote_for_shell(q_in, sizeof q_in, input);
#ifdef _WIN32
    snprintf(command, sizeof command, "cd /d %s && %s < %s 2> %s", q_dir, q_exe, q_in, errors_name);
#else
    snprintf(command, sizeof command, "cd %s || exit 125; ulimit -S -t %d 2>/dev/null; exec %s < %s 2> %s",
             q_dir, CPU_LIMIT_SECONDS, q_exe, q_in, errors_name);
#endif
    output->data = NULL;
    output->size = 0;
    append(output, "", 0);
    fflush(stdout);
    pipe = popen(command, POPEN_MODE);
    if (pipe == NULL) {
        snprintf(r.problem, sizeof r.problem, "the checker could not start it");
        return r;
    }
    while ((n = fread(chunk, 1, sizeof chunk, pipe)) > 0) {
        if (output->size + n > (size_t)OUTPUT_LIMIT) {
            r.too_much = 1;
            printf("  Your program printed more than 8 MB; it is probably stuck in a loop.\n"
                   "  If the checker does not continue, press Ctrl+C.\n");
            fflush(stdout);
            break;
        }
        append(output, chunk, n);
    }
    status = pclose(pipe);
#ifdef _WIN32
    if (status != 0) {
        unsigned long code = (unsigned long)status;
        const char *meaning = "";
        if (code == 0xC0000005UL) meaning = ": invalid memory access";
        else if (code == 0xC00000FDUL) meaning = ": stack overflow";
        else if (code == 0xC0000094UL) meaning = ": division by zero";
        else if (code == 0xC0000409UL || code == 3) meaning = ": the program aborted (often a double free)";
        r.crashed = code > 255;
        if (code > 255) snprintf(r.problem, sizeof r.problem, "it crashed%s (code 0x%08lX)", meaning, code);
        else snprintf(r.problem, sizeof r.problem, "it returned %lu, but main should return 0 after QUIT", code);
    }
#else
    if (status == -1) {
        snprintf(r.problem, sizeof r.problem, "the checker could not wait for it to finish");
    } else if (WIFSIGNALED(status)) {
        int sig = WTERMSIG(status);
        const char *meaning = "it crashed";
        if (sig == SIGSEGV) meaning = "it crashed: invalid memory access (segmentation fault)";
        else if (sig == SIGABRT) meaning = "it crashed: the program aborted (often a double free)";
        else if (sig == SIGFPE) meaning = "it crashed: arithmetic error such as division by zero";
        else if (sig == SIGKILL) meaning = "it was stopped, probably because it ran too long";
#ifdef SIGXCPU
        else if (sig == SIGXCPU) meaning = "it ran too long and was stopped; probably an endless loop";
#endif
        else if (sig == SIGPIPE && r.too_much) meaning = "it was stopped after printing too much";
        r.crashed = strncmp(meaning, "it crashed", 10) == 0;
        snprintf(r.problem, sizeof r.problem, "%s (signal %d)", meaning, sig);
    } else if (WIFEXITED(status) && WEXITSTATUS(status) == 125) {
        snprintf(r.problem, sizeof r.problem, "the checker could not enter the folder runs/<test>");
    } else if (WIFEXITED(status) && WEXITSTATUS(status) == 127 && output->size == 0) {
        snprintf(r.problem, sizeof r.problem, "it could not be started");
    } else if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
        snprintf(r.problem, sizeof r.problem, "it returned %d, but main should return 0 after QUIT",
                 WEXITSTATUS(status));
    }
#endif
    if (r.too_much && r.problem[0] == '\0')
        snprintf(r.problem, sizeof r.problem, "it printed too much output");
    r.ok = r.problem[0] == '\0';
    return r;
}

/* ---------- comparing and explaining ---------- */

/* Compares two texts. When `print` is set, explains the first difference.
 * `stage` is given for standard output so the command can be named. */
static int compare(const char *what, const Text *expected_raw, const Text *actual_raw,
                   const Stage *stage, int print, const Options *o)
{
    Text expected = *expected_raw, actual = *actual_raw;
    Lines e, a;
    size_t d, limit;
    int equal;
    const char *indent = "      ";

    expected.data = malloc(expected_raw->size + 1);
    actual.data = malloc(actual_raw->size + 1);
    if (expected.data == NULL || actual.data == NULL) {
        free(expected.data);
        free(actual.data);
        return 0;
    }
    memcpy(expected.data, expected_raw->data, expected_raw->size + 1);
    memcpy(actual.data, actual_raw->data, actual_raw->size + 1);
    normalize(&expected);
    normalize(&actual);
    equal = expected.size == actual.size && memcmp(expected.data, actual.data, expected.size) == 0;
    if (equal || !print) {
        free(expected.data);
        free(actual.data);
        return equal;
    }

    e = split_lines(&expected);
    a = split_lines(&actual);
    limit = e.count < a.count ? e.count : a.count;
    for (d = 0; d < limit && same_line(e.line[d], a.line[d]); ++d)
        ;

    if (o->brief) {
        if (actual.size == 0) printf("  %s: your program printed nothing\n", what);
        else if (d < limit || d < e.count || d < a.count)
            printf("  %s: first difference at line %lu\n", what, (unsigned long)(d + 1));
        else printf("  %s: the last line does not end with a newline\n", what);
        goto done;
    }

    if (actual.size == 0) {
        printf("  %s: your program printed nothing.\n", what);
        if (stage != NULL)
            printf("    Did it crash, or does it read the commands in a different way?\n");
        printf("    Expected (first lines):\n");
        print_numbered(&e, 0, LINES_SHOWN + CONTEXT_BEFORE, indent);
        goto full;
    }
    if (d == limit && e.count == a.count) {
        printf("  %s: line %lu does not end with a newline (\\n). Every line must end with one.\n",
               what, (unsigned long)e.count);
        goto full;
    }

    printf("  %s: first difference at line %lu", what, (unsigned long)(d + 1));
    if (stage != NULL) {
        size_t command;
        int number = command_line_for(&e, d, stage, &command);
        if (number > 0) {
            printf(", printed by input line %d: ", number);
            print_line(stage->lines.line[command], 0);
        }
    }
    printf("\n");
    if (d > 0) print_numbered(&e, d > CONTEXT_BEFORE ? d - CONTEXT_BEFORE : 0, d, indent);
    printf("    Expected:\n");
    if (d < e.count) print_numbered(&e, d, d + LINES_SHOWN, indent);
    else printf("%s(nothing more: the expected output ends here)\n", indent);
    printf("    Your output:\n");
    if (d < a.count) print_numbered(&a, d, d + LINES_SHOWN, indent);
    else printf("%s(nothing more: your output ends here)\n", indent);

    if (d < e.count && d < a.count && same_without_spaces(e.line[d], a.line[d])) {
        printf("    Hint: the lines differ only in spaces. With spaces shown as '.':\n");
        printf("%sexpected: ", indent);
        print_line(e.line[d], 1);
        printf("\n%syours:    ", indent);
        print_line(a.line[d], 1);
        printf("\n");
    } else if (d >= a.count) {
        printf("    Hint: your output stopped early. Did your program crash or stop reading commands?\n");
    } else if (d >= e.count) {
        printf("    Hint: your output has extra lines at the end.\n");
    } else if (a.line[d].len > e.line[d].len
               && (memcmp(a.line[d].start + a.line[d].len - e.line[d].len, e.line[d].start, e.line[d].len) == 0
                   || memcmp(a.line[d].start, e.line[d].start, e.line[d].len) == 0)) {
        printf("    Hint: your line contains the expected text plus something extra.\n"
               "    Do not print menus, prompts or debug messages; print only the required output.\n");
    }

full:
    if (o->full) {
        printf("    ---- complete expected %s ----\n", what);
        print_numbered(&e, 0, e.count, indent);
        printf("    ---- complete %s of your program ----\n", what);
        print_numbered(&a, 0, a.count, indent);
    }
done:
    free(e.line);
    free(a.line);
    free(expected.data);
    free(actual.data);
    return 0;
}

/* ---------- running one test ---------- */

static void stage_file(char out[PATH_SIZE], const char *base, const char *ext, int stage)
{
    if (stage == 1) snprintf(out, PATH_SIZE, "%s%s", base, ext);
    else snprintf(out, PATH_SIZE, "%s_%d%s", base, stage, ext);
}

/* Checks (print = 0) or explains (print = 1) the results kept in run_dir. */
static int check_results(const char *case_dir, const char *run_dir, Stage *stages, int stage_count,
                         RunResult *runs, int print, const Options *o)
{
    int passed = 1, s, i;
    for (s = 1; s <= stage_count; ++s) {
        char name[PATH_SIZE], expected_path[PATH_SIZE], actual_path[PATH_SIZE], what[PATH_SIZE + 64];
        char expected_dir[PATH_SIZE];
        Text expected, actual;
        Stage *stage = &stages[s - 1];
        const char *run_label = stage_count > 1 ? (s == 1 ? " (first run)" : " (second run)") : "";

        if (!runs[s - 1].ok) {
            passed = 0;
            if (print) printf("  Your program%s: %s.\n", run_label, runs[s - 1].problem);
            if (print && runs[s - 1].crashed && !o->brief)
                printf("    Because it crashed, some of its output may be missing.\n"
                       "    Run it by hand to see where it stops.\n");
        }
        stage_file(name, "expected_output", ".txt", s);
        join(expected_path, case_dir, name);
        stage_file(name, "actual_output", ".txt", s);
        join(actual_path, run_dir, name);
        if (!read_file(expected_path, &expected)) {
            printf("  This test is incomplete: %s is missing.\n", expected_path);
            return 0;
        }
        read_file(actual_path, &actual);
        if (actual.data == NULL) append(&actual, "", 0);
        snprintf(what, sizeof what, "Standard output%s", run_label);
        if (!compare(what, &expected, &actual, stage, print, o)) passed = 0;
        free(expected.data);
        free(actual.data);

        stage_file(expected_dir, "expected_files", "", s);
        for (i = 0; i < stage->saves; ++i) {
            char relative[PATH_SIZE];
            int saved_again = 0, j;
            for (j = i + 1; j < stage->saves; ++j)
                if (strcmp(stage->save[i], stage->save[j]) == 0) saved_again = 1;
            if (saved_again) continue;      /* only the last SAVE to a name counts */
            join(relative, expected_dir, stage->save[i]);
            join(expected_path, case_dir, relative);
            if (!read_file(expected_path, &expected)) continue;   /* this SAVE should fail */
            join(actual_path, run_dir, stage->save[i]);
            snprintf(what, sizeof what, "Saved file %s%s", stage->save[i], run_label);
            if (!read_file(actual_path, &actual)) {
                passed = 0;
                if (print) {
                    if (o->brief) printf("  %s: not created\n", what);
                    else printf("  %s: your program did not create this file (input line %d: SAVE %s).\n",
                                what, stage->save_line[i], stage->save[i]);
                }
            } else {
                if (!compare(what, &expected, &actual, NULL, print, o)) passed = 0;
                free(actual.data);
            }
            free(expected.data);
        }
    }
    return passed;
}

static void show_errors(const char *run_dir, int stage_count, const Options *o)
{
    int s;
    if (o->brief) return;
    for (s = 1; s <= stage_count; ++s) {
        char name[PATH_SIZE], path[PATH_SIZE];
        Text t;
        stage_file(name, "actual_errors", ".txt", s);
        join(path, run_dir, name);
        if (read_file(path, &t) && t.size > 0) {
            Lines l = split_lines(&t);
            printf("  Note: your program wrote to stderr (not graded). First lines:\n");
            print_numbered(&l, 0, 5, "      ");
            free(l.line);
        }
        free(t.data);
    }
}

static int run_case(const char *id, const char *case_dir, const char *runs_root, const char *exe,
                    const Options *o)
{
    static Stage stages[MAX_STAGES];
    static char names[MAX_NAMES][NAME_SIZE];
    RunResult runs[MAX_STAGES];
    char run_dir[PATH_SIZE], path[PATH_SIZE], name[PATH_SIZE], from[PATH_SIZE];
    int stage_count = 0, name_count = 0, s, i, passed;

    for (s = 1; s <= MAX_STAGES; ++s) {
        stage_file(name, "input", ".txt", s);
        join(path, case_dir, name);
        if (!read_file(path, &stages[s - 1].text)) break;
        normalize(&stages[s - 1].text);
        stages[s - 1].lines = split_lines(&stages[s - 1].text);
        stages[s - 1].saves = 0;
        scan_commands(&stages[s - 1], names, &name_count);
        stage_count = s;
    }

    /* A fresh folder: remove what an earlier check left, then copy the test's files. */
    make_dir(runs_root);
    join(run_dir, runs_root, id);
    make_dir(run_dir);
    for (s = 1; s <= MAX_STAGES; ++s) {
        stage_file(name, "actual_output", ".txt", s);
        join(path, run_dir, name);
        remove(path);
        stage_file(name, "actual_errors", ".txt", s);
        join(path, run_dir, name);
        remove(path);
    }
    for (i = 0; i < name_count; ++i) {
        join(path, run_dir, names[i]);
        remove(path);
        join(from, case_dir, names[i]);
        if (is_file(from) && !copy_file(from, path))
            printf("  Warning: could not copy %s into %s\n", names[i], run_dir);
    }

    for (s = 1; s <= stage_count; ++s) {
        Text output;
        stage_file(name, "input", ".txt", s);
        join(path, case_dir, name);
        stage_file(name, "actual_errors", ".txt", s);
        runs[s - 1] = run_program(exe, run_dir, path, name, &output);
        stage_file(name, "actual_output", ".txt", s);
        join(path, run_dir, name);
        write_file(path, &output);
        free(output.data);
    }

    passed = check_results(case_dir, run_dir, stages, stage_count, runs, 0, o);
    printf("%s %s\n", passed ? "PASS" : "FAIL", id);
    if (!passed) {
        check_results(case_dir, run_dir, stages, stage_count, runs, 1, o);
        if (!o->brief) printf("  Your output and saved files are kept in: %s\n", run_dir);
    }
    show_errors(run_dir, stage_count, o);
    for (s = 0; s < stage_count; ++s) {
        free(stages[s].text.data);
        free(stages[s].lines.line);
    }
    return passed;
}

/* ---------- finding the program and the tests ---------- */

static int find_program(const char *given, char out[PATH_SIZE])
{
    char candidate[PATH_SIZE];
    size_t n = strlen(given);
    if (n > 2 && strcmp(given + n - 2, ".c") == 0) {
        printf("%s is a source file. Build your program first (with make), then give the program it built.\n",
               given);
        return 0;
    }
    snprintf(candidate, sizeof candidate, "%s", given);
    if (is_dir(candidate)) {
        char inside[PATH_SIZE];
        join(inside, given, "inventory");
        snprintf(candidate, sizeof candidate, "%s", inside);
    }
#ifdef _WIN32
    if (!is_file(candidate) && strlen(candidate) + 4 < PATH_SIZE) strcat(candidate, ".exe");
#endif
    if (!is_file(candidate) || !full_path(candidate, out)) {
        printf("Cannot find your program: %s\n"
               "Build it with make first, then give the path of the program it built.\n", given);
        return 0;
    }
    return 1;
}

static int find_tests(const char *given, const char *argv0, char out[PATH_SIZE])
{
    char candidate[PATH_SIZE], dir[PATH_SIZE];
    if (given != NULL) {
        if (is_dir(given) && full_path(given, out)) return 1;
        printf("The tests folder does not exist: %s\n", given);
        return 0;
    }
    if (is_dir("tests") && full_path("tests", out)) return 1;
    parent_dir(dir, argv0);             /* next to checker.exe, if started from elsewhere */
    join(candidate, dir, "tests");
    if (is_dir(candidate) && full_path(candidate, out)) return 1;
    printf("Cannot find the \"tests\" folder. Run ./checker in your project folder,\n"
           "the folder that contains \"tests\".\n");
    return 0;
}

static void usage(void)
{
    printf("Usage: ./checker   (run it in your project folder; students need no options)\n"
           "Options for the TA team: checker [program] [test ...] [options]\n"
           "  program        your compiled program; default: inventory (inventory.exe on\n"
           "                 Windows) in the current folder or next to the checker\n"
           "  test           run only these tests, for example P04 P07\n"
           "  --tests DIR    folder that holds the tests (default: tests)\n"
           "  --runs DIR     folder for your output and saved files (default: runs next to tests)\n"
           "  --full         on failure, also print the complete expected and actual output\n"
           "  --brief        one line per problem\n");
}

static void normalize_id(char *id)
{
    char *p;
    for (p = id; *p != '\0'; ++p)
        if (*p >= 'a' && *p <= 'z') *p = (char)(*p - 'a' + 'A');
    if (strlen(id) == 2 && id[1] >= '0' && id[1] <= '9') {   /* P4 -> P04 */
        id[3] = '\0';
        id[2] = id[1];
        id[1] = '0';
    }
}

/* P4, p04, H10 ... (a test name rather than a program path) */
static int looks_like_test_id(const char *arg)
{
    size_t n = strlen(arg);
    return (n == 2 || n == 3) && strchr("PpHh", arg[0]) != NULL && arg[1] >= '0' && arg[1] <= '9'
        && (n == 2 || (arg[2] >= '0' && arg[2] <= '9')) && !is_file(arg);
}

/* Looks for inventory(.exe) in the current folder, then next to the checker. */
static int default_program(const char *argv0, char out[PATH_SIZE])
{
    const char *names[] = {
#ifdef _WIN32
        "inventory.exe",
#endif
        "inventory"
    };
    char dir[PATH_SIZE], candidate[PATH_SIZE];
    size_t i;
    for (i = 0; i < sizeof names / sizeof names[0]; ++i)
        if (is_file(names[i]) && full_path(names[i], out)) return 1;
    parent_dir(dir, argv0);
    for (i = 0; i < sizeof names / sizeof names[0]; ++i) {
        join(candidate, dir, names[i]);
        if (is_file(candidate) && full_path(candidate, out)) return 1;
    }
    return 0;
}

/* Windows: true when the checker got its own console window, that is, it was
 * double-clicked in File Explorer instead of started from a terminal. */
static int started_by_double_click(void)
{
#ifdef _WIN32
    DWORD processes[2];
    return GetConsoleProcessList(processes, 2) == 1;
#else
    return 0;
#endif
}

int main(int argc, char **argv)
{
    static char ids[MAX_CASES][8];
    char program_arg[PATH_SIZE] = "", program[PATH_SIZE], tests[PATH_SIZE], runs_root[PATH_SIZE];
    char wanted[MAX_CASES][8];
    const char *tests_arg = NULL, *runs_arg = NULL;
    Options o = {0, 0};
    int wanted_count = 0, case_count = 0, passed = 0, i, n;
    const char prefixes[] = "PH";

#ifdef _WIN32
    /* A crashing program should not open a Windows error dialog and block the checker. */
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif
    if (started_by_double_click()) {
        printf("Run the checker from a terminal in your project folder, for example in the\n"
               "VS Code terminal:  ./checker\n"
               "(Double-clicking checker.exe does not run the tests.)\n\n"
               "Press Enter to close.\n");
        fflush(stdout);
        getchar();
        return 2;
    }
    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) { usage(); return 0; }
        else if (strcmp(argv[i], "--full") == 0) o.full = 1;
        else if (strcmp(argv[i], "--brief") == 0) o.brief = 1;
        else if (strcmp(argv[i], "--tests") == 0 && i + 1 < argc) tests_arg = argv[++i];
        else if (strcmp(argv[i], "--runs") == 0 && i + 1 < argc) runs_arg = argv[++i];
        else if (argv[i][0] == '-') { printf("Unknown option: %s\n", argv[i]); usage(); return 2; }
        else if (program_arg[0] == '\0' && !looks_like_test_id(argv[i]))
            snprintf(program_arg, sizeof program_arg, "%s", argv[i]);
        else if (wanted_count < MAX_CASES && looks_like_test_id(argv[i])) {
            snprintf(wanted[wanted_count], sizeof wanted[0], "%s", argv[i]);
            normalize_id(wanted[wanted_count++]);
        } else { printf("Not a test name: %s\n", argv[i]); return 2; }
    }

    if (program_arg[0] != '\0') {
        if (!find_program(program_arg, program)) return 2;
    } else if (!default_program(argv[0], program)) {
        printf("Cannot find your program (inventory) in this folder.\n"
               "Build it first with make (Windows: mingw32-make) in your project folder,\n"
               "then run ./checker again.\n");
        return 2;
    }

    if (!find_tests(tests_arg, argv[0], tests)) return 2;
    if (runs_arg != NULL) {
        make_dirs(runs_arg);
        if (!full_path(runs_arg, runs_root)) { printf("Cannot create the folder %s\n", runs_arg); return 2; }
    } else {                               /* runs/ next to tests/ */
        char parent[PATH_SIZE];
        parent_dir(parent, tests);
        join(runs_root, parent, "runs");
    }

    for (i = 0; prefixes[i] != '\0'; ++i) {
        for (n = 1; n <= 99 && case_count < MAX_CASES; ++n) {
            char id[8], dir[PATH_SIZE], input[PATH_SIZE];
            snprintf(id, sizeof id, "%c%02d", prefixes[i], n);
            join(dir, tests, id);
            join(input, dir, "input.txt");
            if (is_file(input)) snprintf(ids[case_count++], sizeof ids[0], "%s", id);
        }
    }
    for (i = 0; i < wanted_count; ++i) {
        int found = 0, k;
        for (k = 0; k < case_count; ++k)
            if (strcmp(ids[k], wanted[i]) == 0) found = 1;
        if (!found) { printf("There is no test %s in %s\n", wanted[i], tests); return 2; }
    }
    if (case_count == 0) { printf("No tests found in %s\n", tests); return 2; }

    printf("Program: %s\nTests:   %s\n\n", program, tests);
    n = 0;
    for (i = 0; i < case_count; ++i) {
        char dir[PATH_SIZE];
        int k, selected = wanted_count == 0;
        for (k = 0; k < wanted_count; ++k)
            if (strcmp(ids[i], wanted[k]) == 0) selected = 1;
        if (!selected) continue;
        join(dir, tests, ids[i]);
        passed += run_case(ids[i], dir, runs_root, program, &o);
        ++n;
    }
    printf("\nResult: %d of %d tests passed.\n", passed, n);
    return passed == n ? 0 : 1;
}
