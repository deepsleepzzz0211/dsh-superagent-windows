/*
 * lark-bot: a spawnable lark-cli wrapper for dsh-superagent on Windows.
 *
 * dsh-superagent's Feishu Source needs a Source CLI that is (a) a real
 * executable (Node cannot spawn .cmd/.bat/.sh), (b) pinned to a bot-only
 * lark-cli configuration, and (c) robust when DSH runs commands through
 * Git Bash's sh.exe on Windows. This wrapper provides all four fixes:
 *
 *   1. pins LARKSUITE_CLI_CONFIG_DIR to a bot-only lark-cli config dir,
 *   2. repairs MSYS path rewriting (sh.exe turns /open-apis/... into
 *      <msys-root>/open-apis/..., which makes the Lark API answer 404),
 *   3. replaces stdin with a self-held pipe: lark-cli's `event consume`
 *      exits on stdin EOF, and Node's overlapped stdin pipe reads as an
 *      instant EOF (dsh-superagent stops the consumer with SIGTERM),
 *   4. re-quotes the command line: MSVC _execv joins argv without quoting,
 *      which splits multi-word arguments (e.g. Chinese reply text) into
 *      positional fragments; CreateProcess with explicit quoting fixes it.
 *
 * Environment variables (set these before starting dsh-superagent):
 *   LARK_BOT_CLI          absolute path of the real lark-cli.exe   (required)
 *   LARK_BOT_CONFIG_DIR   bot-only lark-cli config directory       (default:
 *                         %USERPROFILE%\.lark-cli-dsh-superagent)
 *   LARK_MSYS_ROOT        your Git installation root with a trailing slash
 *                         (e.g. D:/Git/); only needed when DSH starts worker
 *                         commands through Git Bash's sh.exe; omit to disable
 *                         the path repair
 *
 * Build (MinGW-w64 gcc):
 *   gcc -O2 -o lark-bot.exe larkbot.c
 * Then point the Feishu Source's "Bot lark-cli" at the absolute path of
 * lark-bot.exe.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <io.h>
#include <fcntl.h>
#include <process.h>
#include <windows.h>

static char msys_root[MAX_PATH] = {0};
static int msys_root_len = 0;

/* Rewrites "<msys-root>open-apis/..." back to "/open-apis/..." when the
   argument was mangled by MSYS path conversion; returns arg otherwise. */
static const char *repair_msys_path(const char *arg, char *buf, size_t bufsize) {
    if (msys_root_len > 0 && strncmp(arg, msys_root, (size_t)msys_root_len) == 0) {
        if (bufsize > strlen(arg)) {
            buf[0] = '/';
            strcpy(buf + 1, arg + msys_root_len);
            return buf;
        }
    }
    return arg;
}

static void append_quoted(char *dst, size_t *n, size_t cap, const char *arg) {
    size_t need = strlen(arg) * 2 + 3;
    char *tmp = (char *)malloc(need);
    if (!tmp) { dst[*n] = 0; return; }
    char *w = tmp;
    *w++ = '"';
    const char *p = arg;
    for (;;) {
        int bs = 0;
        while (*p == '\\') { bs++; p++; }
        if (!*p) { for (int i = 0; i < bs * 2; i++) *w++ = '\\'; break; }
        if (*p == '"') {
            for (int i = 0; i < bs * 2 + 1; i++) *w++ = '\\';
            *w++ = '"';
        } else {
            for (int i = 0; i < bs; i++) *w++ = '\\';
            *w++ = *p;
        }
        p++;
    }
    *w++ = '"';
    *w = 0;
    size_t l = strlen(tmp);
    if (*n + l + 2 >= cap) { free(tmp); return; }
    memcpy(dst + *n, tmp, l + 1);
    *n += l;
    free(tmp);
}

int main(int argc, char *argv[]) {
    const char *cli = getenv("LARK_BOT_CLI");
    if (!cli || !*cli) {
        fprintf(stderr, "lark-bot: LARK_BOT_CLI is not set; point it at the absolute path of lark-cli.exe\n");
        return 125;
    }
    const char *config_dir = getenv("LARK_BOT_CONFIG_DIR");
    if (!config_dir || !*config_dir) {
        /* Default matches the layout suggested by dsh-superagent's docs. */
        const char *home = getenv("USERPROFILE");
        if (!home || !*home) home = getenv("HOME");
        if (!home || !*home) {
            fprintf(stderr, "lark-bot: cannot resolve the config dir; set LARK_BOT_CONFIG_DIR\n");
            return 125;
        }
        static char default_dir[MAX_PATH];
        _snprintf(default_dir, sizeof(default_dir), "%s\\.lark-cli-dsh-superagent", home);
        config_dir = default_dir;
    }
    static char env_dir[MAX_PATH];
    _snprintf(env_dir, sizeof(env_dir), "LARKSUITE_CLI_CONFIG_DIR=%s", config_dir);
    _putenv(env_dir);

    const char *msys = getenv("LARK_MSYS_ROOT");
    if (msys && *msys) {
        _snprintf(msys_root, sizeof(msys_root), "%s", msys);
        /* Normalize: accept both "D:/Git/" and "D:\Git\" and missing trailing slash. */
        for (char *c = msys_root; *c; c++) if (*c == '\\') *c = '/';
        msys_root_len = (int)strlen(msys_root);
        if (msys_root_len > 0 && msys_root[msys_root_len - 1] != '/' && msys_root_len + 1 < (int)sizeof(msys_root)) {
            msys_root[msys_root_len++] = '/';
            msys_root[msys_root_len] = 0;
        }
    }

    /* stdin: a self-held pipe that never EOFs (see header comment). */
    int fds[2];
    if (_pipe(fds, 4096, _O_BINARY) != 0) { perror("_pipe"); return 125; }
    if (_dup2(fds[0], 0) != 0) { perror("_dup2"); return 125; }
    _close(fds[0]);
    /* fds[1] stays open for the lifetime of the process: no stdin EOF. */

    static char cmdline[32768];
    size_t n = 0;
    n = (size_t)_snprintf(cmdline, sizeof(cmdline), "\"%s\"", cli);
    static char buf[32768];
    for (int i = 1; i < argc; i++) {
        const char *a = repair_msys_path(argv[i], buf, sizeof(buf));
        if (n + strlen(a) * 2 + 4 >= sizeof(cmdline)) {
            fprintf(stderr, "lark-bot: command line too long\n");
            return 126;
        }
        cmdline[n++] = ' ';
        append_quoted(cmdline, &n, sizeof(cmdline), a);
        cmdline[n] = 0;
    }

    STARTUPINFOA si; PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));
    if (!CreateProcessA(cli, cmdline, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        fprintf(stderr, "lark-bot: CreateProcess failed: %lu\n", GetLastError());
        return 127;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    return (int)code;
}
