#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

typedef struct {
    int type;                  // 0: 8=press, 9=release
    void *window;              // 8
    int8_t send_event;         // 16
    uint32_t time;             // 20
    uint32_t state;            // 24 (1=shift, 4=ctrl, 8=alt)
    uint32_t keyval;           // 28
    int32_t length;            // 32
    char *string;              // 40
    uint16_t hardware_keycode; // 48
    uint8_t group;             // 50
    uint32_t is_modifier : 1;  // 51
} GdkEventKey_t;

typedef int (*GtkKeySnooperFunc)(void *grab_widget, GdkEventKey_t *event, void *func_data);
typedef uint32_t (*gtk_key_snooper_install_fn)(GtkKeySnooperFunc snooper, void *func_data);
typedef uint32_t (*g_timeout_add_fn)(uint32_t interval, int (*function)(void *), void *data);

// Code::Blocks & wxScintilla function pointers
typedef void* (*Manager_Get_fn)(void);
typedef void* (*Manager_GetEditorManager_fn)(void *mgr);
typedef void* (*EditorManager_GetActiveEditor_fn)(void *emgr);
typedef void* (*EditorManager_GetBuiltinEditor_fn)(void *emgr, void *ebase);
typedef void* (*cbEditor_GetControl_fn)(void *cbed);
typedef long  (*wxScintilla_SendMsg_fn)(void *sci, int msg, unsigned long wp, long lp);

static Manager_Get_fn fn_Manager_Get = NULL;
static Manager_GetEditorManager_fn fn_GetEditorManager = NULL;
static EditorManager_GetActiveEditor_fn fn_GetActiveEditor = NULL;
static EditorManager_GetBuiltinEditor_fn fn_GetBuiltinEditor = NULL;
static cbEditor_GetControl_fn fn_GetControl = NULL;
static wxScintilla_SendMsg_fn fn_SendMsg = NULL;

// Scintilla constants
#define SCI_GETLENGTH               2006
#define SCI_GETTEXT                 2182
#define SCI_STYLESETFORE            2051
#define SCI_STYLESETBACK            2052
#define SCI_STYLESETBOLD            2053
#define SCI_STYLESETITALIC          2054
#define SCI_INDICSETSTYLE           2080
#define SCI_INDICSETFORE            2082
#define SCI_SETINDICATORCURRENT     2500
#define SCI_INDICATORFILLRANGE      2504
#define SCI_INDICATORCLEARRANGE     2505
#define SCI_INDICSETALPHA           2523
#define SCI_POSITIONFROMLINE        2167
#define SCI_GETLINEENDPOSITION      2136
#define SCI_GETLINECOUNT            2154
#define SCI_SETSELALPHA             2478
#define SCI_ANNOTATIONSETTEXT       2540
#define SCI_ANNOTATIONSETSTYLE      2542
#define SCI_ANNOTATIONCLEARALL      2547
#define SCI_ANNOTATIONSETVISIBLE    2548

#define INDIC_SQUIGGLE              1
#define INDIC_LSP_ERROR             28
#define INDIC_LSP_WARN              29
#define STYLE_ANNOTATION_ERR        250
#define STYLE_ANNOTATION_WARN       251

typedef struct {
    int line;       // 0-based line
    int is_error;   // 1=error, 0=warning
    char msg[512];
} LspDiag;

static pthread_mutex_t lsp_mutex = PTHREAD_MUTEX_INITIALIZER;
static LspDiag lsp_diags[64];
static int lsp_diag_count = 0;
static int lsp_results_ready = 0;
static int lsp_running = 0;
static int lsp_enabled = 1;
static uint64_t last_hash = 0;
static uint64_t pending_hash = 0;
static int unchanged_ticks = 0;
static char *pending_code = NULL;

static uint64_t hash_str(const char *s, size_t len) {
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < len; i++) {
        h ^= (unsigned char)s[i];
        h *= 1099511628211ULL;
    }
    return h;
}

static void* lsp_worker_thread(void *arg) {
    char *code = (char *)arg;
    char tmp_src[128];
    snprintf(tmp_src, sizeof(tmp_src), "/tmp/.cb_lsp_%d.c", (int)getpid());

    FILE *f = fopen(tmp_src, "w");
    if (!f) {
        free(code);
        pthread_mutex_lock(&lsp_mutex);
        lsp_running = 0;
        pthread_mutex_unlock(&lsp_mutex);
        return NULL;
    }
    fwrite(code, 1, strlen(code), f);
    fclose(f);

    // Detect if C++ headers like <iostream>, <vector>, <bits/stdc++.h>, using namespace std are used
    int is_cpp = (strstr(code, "<iostream>") || strstr(code, "<vector>") ||
                  strstr(code, "<bits/") || strstr(code, "using namespace") ||
                  strstr(code, "std::")) ? 1 : 0;
    free(code);

    char cmd[512];
    snprintf(cmd, sizeof(cmd),
             "%s -x %s -fsyntax-only -Wall -Wextra -Wformat=2 -Wno-unused-parameter -fdiagnostics-color=never '%s' 2>&1",
             is_cpp ? "g++" : "gcc",
             is_cpp ? "c++" : "c",
             tmp_src);

    LspDiag local_diags[64];
    int local_count = 0;

    FILE *p = popen(cmd, "r");
    if (p) {
        char linebuf[1024];
        while (fgets(linebuf, sizeof(linebuf), p) && local_count < 64) {
            // Match /tmp/.cb_lsp_...:LINE:COL: error|warning: MSG
            char *colon1 = strstr(linebuf, tmp_src);
            if (!colon1) continue;
            colon1 += strlen(tmp_src);
            if (*colon1 != ':') continue;
            int lnum = atoi(colon1 + 1);
            if (lnum <= 0) continue;

            char *err_tag = strstr(colon1, ": error: ");
            char *warn_tag = strstr(colon1, ": warning: ");
            char *fatal_tag = strstr(colon1, ": fatal error: ");
            int is_err = 0;
            char *msg_start = NULL;
            if (fatal_tag) {
                is_err = 1;
                msg_start = fatal_tag + 15;
            } else if (err_tag) {
                is_err = 1;
                msg_start = err_tag + 9;
            } else if (warn_tag) {
                is_err = 0;
                msg_start = warn_tag + 11;
            }
            if (msg_start) {
                // Strip trailing newline
                size_t mlen = strlen(msg_start);
                while (mlen > 0 && (msg_start[mlen - 1] == '\n' || msg_start[mlen - 1] == '\r')) {
                    msg_start[--mlen] = '\0';
                }
                // Merge if same line already exists
                int merged = 0;
                for (int i = 0; i < local_count; i++) {
                    if (local_diags[i].line == lnum - 1) {
                        if (is_err) local_diags[i].is_error = 1;
                        merged = 1;
                        break;
                    }
                }
                if (!merged) {
                    local_diags[local_count].line = lnum - 1;
                    local_diags[local_count].is_error = is_err;
                    snprintf(local_diags[local_count].msg, sizeof(local_diags[local_count].msg),
                             "  %s %s ", is_err ? "✖ [Error]" : "⚠ [Warning]", msg_start);
                    local_count++;
                }
            }
        }
        pclose(p);
    }
    unlink(tmp_src);

    pthread_mutex_lock(&lsp_mutex);
    memcpy(lsp_diags, local_diags, sizeof(LspDiag) * local_count);
    lsp_diag_count = local_count;
    lsp_results_ready = 1;
    lsp_running = 0;
    pthread_mutex_unlock(&lsp_mutex);
    return NULL;
}

static void* get_active_scintilla(void) {
    if (!fn_Manager_Get || !fn_GetEditorManager || !fn_GetActiveEditor || !fn_GetBuiltinEditor || !fn_GetControl)
        return NULL;
    void *mgr = fn_Manager_Get();
    if (!mgr) return NULL;
    void *emgr = fn_GetEditorManager(mgr);
    if (!emgr) return NULL;
    void *ebase = fn_GetActiveEditor(emgr);
    if (!ebase) return NULL;
    void *cbed = fn_GetBuiltinEditor(emgr, ebase);
    if (!cbed) return NULL;
    return fn_GetControl(cbed);
}

static int lsp_timer_tick(void *user_data) {
    (void)user_data;
    void *sci = get_active_scintilla();
    if (!sci || !fn_SendMsg) return 1;

    // 1. Enforce 50% opacity brace highlight & selection alpha on live Scintilla control
    // Scintilla colour format is BGR: 0x00BBGGRR
    fn_SendMsg(sci, SCI_STYLESETBACK, 34, 0x58462A); // #2A4658 (50% steel-blue)
    fn_SendMsg(sci, SCI_STYLESETFORE, 34, 0xDCDCDC); // #DCDCDC
    fn_SendMsg(sci, SCI_STYLESETBOLD, 34, 0);
    fn_SendMsg(sci, SCI_STYLESETBACK, 35, 0x232378); // #782323 (unmatched brace)
    fn_SendMsg(sci, SCI_SETSELALPHA, 128, 0);        // 50% selection transparency
    // VS Code typography: +2px top/bottom line padding (1.35x line height) + LCD subpixel AA + buffered draw
    fn_SendMsg(sci, 2525, 2, 0); // SCI_SETEXTRAASCENT
    fn_SendMsg(sci, 2527, 2, 0); // SCI_SETEXTRADESCENT
    fn_SendMsg(sci, 2611, 3, 0); // SCI_SETFONTQUALITY (SC_EFF_QUALITY_LCD_OPTIMIZED)
    fn_SendMsg(sci, 2035, 1, 0); // SCI_SETBUFFEREDDRAW

    // Configure LSP squiggle indicators & Error Lens annotation styles
    fn_SendMsg(sci, SCI_INDICSETSTYLE, INDIC_LSP_ERROR, INDIC_SQUIGGLE);
    fn_SendMsg(sci, SCI_INDICSETFORE, INDIC_LSP_ERROR, 0x4747F4);  // #F44747 Red
    fn_SendMsg(sci, SCI_INDICSETSTYLE, INDIC_LSP_WARN, INDIC_SQUIGGLE);
    fn_SendMsg(sci, SCI_INDICSETFORE, INDIC_LSP_WARN, 0x00A7CC);   // #CCA700 Yellow

    fn_SendMsg(sci, SCI_STYLESETBACK, STYLE_ANNOTATION_ERR, 0x201D3B);  // Subtle dark red bg
    fn_SendMsg(sci, SCI_STYLESETFORE, STYLE_ANNOTATION_ERR, 0x7187F4);  // Soft coral red text
    fn_SendMsg(sci, SCI_STYLESETITALIC, STYLE_ANNOTATION_ERR, 1);

    fn_SendMsg(sci, SCI_STYLESETBACK, STYLE_ANNOTATION_WARN, 0x1A2D38); // Subtle dark amber bg
    fn_SendMsg(sci, SCI_STYLESETFORE, STYLE_ANNOTATION_WARN, 0x40C0E5); // Soft gold text
    fn_SendMsg(sci, SCI_STYLESETITALIC, STYLE_ANNOTATION_WARN, 1);

    if (!lsp_enabled) {
        fn_SendMsg(sci, SCI_ANNOTATIONCLEARALL, 0, 0);
        return 1;
    }

    long len = fn_SendMsg(sci, SCI_GETLENGTH, 0, 0);
    if (len <= 0 || len > 256000) return 1;

    // 2. Apply any completed LSP diagnostics to the editor
    pthread_mutex_lock(&lsp_mutex);
    if (lsp_results_ready) {
        lsp_results_ready = 0;
        fn_SendMsg(sci, SCI_SETINDICATORCURRENT, INDIC_LSP_ERROR, 0);
        fn_SendMsg(sci, SCI_INDICATORCLEARRANGE, 0, len);
        fn_SendMsg(sci, SCI_SETINDICATORCURRENT, INDIC_LSP_WARN, 0);
        fn_SendMsg(sci, SCI_INDICATORCLEARRANGE, 0, len);
        fn_SendMsg(sci, SCI_ANNOTATIONCLEARALL, 0, 0);
        fn_SendMsg(sci, SCI_ANNOTATIONSETVISIBLE, 2, 0); // ANNOTATION_BOXED

        long line_count = fn_SendMsg(sci, SCI_GETLINECOUNT, 0, 0);
        for (int i = 0; i < lsp_diag_count; i++) {
            int ln = lsp_diags[i].line;
            if (ln >= 0 && ln < line_count) {
                long start_pos = fn_SendMsg(sci, SCI_POSITIONFROMLINE, ln, 0);
                long end_pos = fn_SendMsg(sci, SCI_GETLINEENDPOSITION, ln, 0);
                // Skip leading indentation on the line for the squiggle
                if (end_pos > start_pos) {
                    fn_SendMsg(sci, SCI_SETINDICATORCURRENT,
                               lsp_diags[i].is_error ? INDIC_LSP_ERROR : INDIC_LSP_WARN, 0);
                    fn_SendMsg(sci, SCI_INDICATORFILLRANGE, start_pos, end_pos - start_pos);
                }
                fn_SendMsg(sci, SCI_ANNOTATIONSETTEXT, ln, (long)lsp_diags[i].msg);
                fn_SendMsg(sci, SCI_ANNOTATIONSETSTYLE, ln,
                           lsp_diags[i].is_error ? STYLE_ANNOTATION_ERR : STYLE_ANNOTATION_WARN);
            }
        }
    }
    int busy = lsp_running;
    pthread_mutex_unlock(&lsp_mutex);

    if (busy) return 1;

    // 3. Check if buffer changed and user paused typing
    char *buf = (char *)malloc(len + 2);
    if (!buf) return 1;
    fn_SendMsg(sci, SCI_GETTEXT, len + 1, (long)buf);
    buf[len] = '\0';
    uint64_t h = hash_str(buf, len);

    if (h != pending_hash) {
        pending_hash = h;
        unchanged_ticks = 0;
        free(buf);
        return 1;
    }

    unchanged_ticks++;
    if (unchanged_ticks >= 1 && h != last_hash) {
        last_hash = h;
        pthread_mutex_lock(&lsp_mutex);
        lsp_running = 1;
        pthread_mutex_unlock(&lsp_mutex);

        pthread_t tid;
        if (pthread_create(&tid, NULL, lsp_worker_thread, buf) == 0) {
            pthread_detach(tid);
        } else {
            free(buf);
            pthread_mutex_lock(&lsp_mutex);
            lsp_running = 0;
            pthread_mutex_unlock(&lsp_mutex);
        }
    } else {
        free(buf);
    }

    return 1;
}

static void launch_debugger(int asan) {
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "$HOME/.local/bin/cb_single_file_debug.sh %s &", asan ? "--asan" : "");
    system(cmd);
}

static int vscode_key_snooper(void *grab_widget, GdkEventKey_t *event, void *func_data) {
    (void)grab_widget;
    (void)func_data;
    if (!event) return 0;

    // F5 / Ctrl+F5 -> Single-file visual debugger / ASan crash analyzer
    if (event->keyval == 0xffc2) {
        if (event->type == 8) {
            int asan = (event->state & 4) != 0;
            launch_debugger(asan);
        }
        return 1;
    }

    // Ctrl+Shift+M -> Toggle inline LSP Error Lens annotations
    if ((event->state & 4) && (event->state & 1) && (event->keyval == 'm' || event->keyval == 'M')) {
        if (event->type == 8) {
            lsp_enabled = !lsp_enabled;
            last_hash = 0;
        }
        return 1;
    }

    // Ctrl + I / J / K / L navigation
    if (event->state & 4) {
        uint32_t kv = event->keyval;

        if (kv == 'i' || kv == 'I') {
            event->keyval = 0xff52; // Up
            event->hardware_keycode = 111;
            event->state &= ~4;
            return 0;
        }
        if (kv == 'k' || kv == 'K') {
            event->keyval = 0xff54; // Down
            event->hardware_keycode = 116;
            event->state &= ~4;
            return 0;
        }
        if (kv == 'j' || kv == 'J') {
            event->keyval = 0xff51; // Left
            event->hardware_keycode = 113;
            event->state &= ~4;
            if (event->state & 8) {
                event->state &= ~8;
                event->state |= 4;
            }
            return 0;
        }
        if (kv == 'l' || kv == 'L') {
            event->keyval = 0xff53; // Right
            event->hardware_keycode = 114;
            event->state &= ~4;
            if (event->state & 8) {
                event->state &= ~8;
                event->state |= 4;
            }
            return 0;
        }
    }
    return 0;
}

__attribute__((constructor))
static void init_vscode_helper() {
    void *gtk = dlopen("libgtk-3.so.0", RTLD_LAZY | RTLD_NOLOAD);
    if (!gtk) gtk = dlopen("libgtk-3.so.0", RTLD_LAZY);
    if (gtk) {
        gtk_key_snooper_install_fn install = (gtk_key_snooper_install_fn)dlsym(gtk, "gtk_key_snooper_install");
        if (install) {
            install(vscode_key_snooper, NULL);
        }
        g_timeout_add_fn timeout_add = (g_timeout_add_fn)dlsym(gtk, "g_timeout_add");
        if (timeout_add) {
            timeout_add(650, lsp_timer_tick, NULL);
        }
    }

    void *cb = dlopen("libcodeblocks.so.0", RTLD_LAZY | RTLD_NOLOAD);
    if (!cb) cb = dlopen("/lib/libcodeblocks.so.0", RTLD_LAZY);
    if (cb) {
        fn_Manager_Get = (Manager_Get_fn)dlsym(cb, "_ZN7Manager3GetEv");
        fn_GetEditorManager = (Manager_GetEditorManager_fn)dlsym(cb, "_ZNK7Manager16GetEditorManagerEv");
        fn_GetActiveEditor = (EditorManager_GetActiveEditor_fn)dlsym(cb, "_ZN13EditorManager15GetActiveEditorEv");
        fn_GetBuiltinEditor = (EditorManager_GetBuiltinEditor_fn)dlsym(cb, "_ZN13EditorManager16GetBuiltinEditorEP10EditorBase");
        fn_GetControl = (cbEditor_GetControl_fn)dlsym(cb, "_ZNK8cbEditor10GetControlEv");
        fn_SendMsg = (wxScintilla_SendMsg_fn)dlsym(cb, "_ZNK11wxScintilla7SendMsgEiml");
    }
}