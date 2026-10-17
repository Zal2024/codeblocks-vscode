#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <dirent.h>
#include <sys/stat.h>
#include <wchar.h>

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
typedef void* (*Manager_GetProjectManager_fn)(void *mgr);
typedef void* (*EditorManager_GetActiveEditor_fn)(void *emgr);
typedef void* (*EditorManager_GetBuiltinEditor_fn)(void *emgr, void *ebase);
typedef void* (*cbEditor_GetControl_fn)(void *cbed);
typedef long  (*wxScintilla_SendMsg_fn)(void *sci, int msg, unsigned long wp, long lp);

// ProjectManager & wxString helpers
typedef void* (*cbC2U_fn)(void *wxstr_out, const char *utf8);
typedef void* (*ProjectManager_LoadProject_fn)(void *pmgr, const void *wxstr_path, int activateIt);
typedef int   (*ProjectManager_CloseAllProjects_fn)(void *pmgr, int dontsave);
typedef void* (*ProjectManager_AddFileToProject_fn)(void *pmgr, const void *wxstr_path, void *proj, int target);
typedef void* (*ProjectManager_GetWorkspace_fn)(void *pmgr);
typedef void  (*cbProject_SetModified_fn)(void *proj, int modified);
typedef void  (*cbWorkspace_SetModified_fn)(void *ws, int modified);

// Mouse hover helpers
typedef void  (*wxGetMousePosition_fn)(int *x, int *y);
typedef void  (*wxWindow_DoScreenToClient_fn)(void *win, int *x, int *y);

static Manager_Get_fn fn_Manager_Get = NULL;
static Manager_GetEditorManager_fn fn_GetEditorManager = NULL;
static Manager_GetProjectManager_fn fn_GetProjectManager = NULL;
static EditorManager_GetActiveEditor_fn fn_GetActiveEditor = NULL;
static EditorManager_GetBuiltinEditor_fn fn_GetBuiltinEditor = NULL;
static cbEditor_GetControl_fn fn_GetControl = NULL;
static wxScintilla_SendMsg_fn fn_SendMsg = NULL;

static cbC2U_fn fn_cbC2U = NULL;
static ProjectManager_LoadProject_fn fn_LoadProject = NULL;
static ProjectManager_CloseAllProjects_fn fn_CloseAllProjects = NULL;
static ProjectManager_AddFileToProject_fn fn_AddFileToProject = NULL;
static ProjectManager_GetWorkspace_fn fn_GetWorkspace = NULL;
static cbProject_SetModified_fn fn_ProjSetModified = NULL;
static cbWorkspace_SetModified_fn fn_WsSetModified = NULL;
static wxGetMousePosition_fn fn_wxGetMousePos = NULL;
static wxWindow_DoScreenToClient_fn fn_ScreenToClient = NULL;

// Scintilla constants
#define SCI_GETLENGTH               2006
#define SCI_POSITIONFROMPOINTCLOSE  2023
#define SCI_SETBUFFEREDDRAW         2035
#define SCI_STYLESETFORE            2051
#define SCI_STYLESETBACK            2052
#define SCI_STYLESETBOLD            2053
#define SCI_STYLESETITALIC          2054
#define SCI_SETCARETFORE            2069
#define SCI_INDICSETSTYLE           2080
#define SCI_INDICSETFORE            2082
#define SCI_SETCARETLINEBACK        2098
#define SCI_POSITIONFROMLINE        2167
#define SCI_GETLINEENDPOSITION      2136
#define SCI_LINEFROMPOSITION        2166
#define SCI_GETTEXT                 2182
#define SCI_CALLTIPSHOW             2200
#define SCI_CALLTIPCANCEL           2201
#define SCI_CALLTIPACTIVE           2202
#define SCI_CALLTIPSETBACK          2205
#define SCI_CALLTIPSETFORE          2206
#define SCI_SETFOLDMARGINCOLOUR     2290
#define SCI_SETFOLDMARGINHICOLOUR   2291
#define SCI_SETSELALPHA             2478
#define SCI_STYLEGETBACK            2482
#define SCI_SETINDICATORCURRENT     2500
#define SCI_INDICATORCLEARRANGE     2505
#define SCI_INDICATORFILLRANGE      2504
#define SCI_INDICSETALPHA           2523
#define SCI_SETEXTRAASCENT          2525
#define SCI_SETEXTRADESCENT         2527
#define SCI_ANNOTATIONSETTEXT       2540
#define SCI_ANNOTATIONSETSTYLE      2542
#define SCI_ANNOTATIONCLEARALL      2547
#define SCI_ANNOTATIONSETVISIBLE    2548
#define SCI_INDICSETOUTLINEALPHA    2558
#define SCI_SETFONTQUALITY          2611

#define INDIC_SQUIGGLE              1
#define ANNOTATION_BOXED            2

#define INDIC_LSP_ERROR             10
#define INDIC_LSP_WARN              11
#define STYLE_LSP_ERROR             60
#define STYLE_LSP_WARN              61

typedef struct {
    int line;       // 0-based line
    int is_err;     // 1=error, 0=warning
    char msg[384];
} DiagItem;

static DiagItem g_diags[64];
static int g_diag_count = 0;
static uint64_t g_last_hash = 0;
static void *g_last_sci = NULL;
static int g_show_annotations = 0; // Default: yellow warnings only show squiggle + hover tooltip
static int g_checker_busy = 0;
static int g_hover_calltip_active = 0;
static int g_last_hover_line = -1;
static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;

// Auto-workspace tracking
static char g_workspace_dir[1024] = "";
static char g_seen_files[256][256];
static int g_seen_count = 0;

static void resolve_cb_symbols(void) {
    if (fn_Manager_Get) return;
    fn_Manager_Get = (Manager_Get_fn)dlsym(RTLD_DEFAULT, "_ZN7Manager3GetEv");
    fn_GetEditorManager = (Manager_GetEditorManager_fn)dlsym(RTLD_DEFAULT, "_ZNK7Manager16GetEditorManagerEv");
    fn_GetProjectManager = (Manager_GetProjectManager_fn)dlsym(RTLD_DEFAULT, "_ZNK7Manager17GetProjectManagerEv");
    fn_GetActiveEditor = (EditorManager_GetActiveEditor_fn)dlsym(RTLD_DEFAULT, "_ZN13EditorManager15GetActiveEditorEv");
    fn_GetBuiltinEditor = (EditorManager_GetBuiltinEditor_fn)dlsym(RTLD_DEFAULT, "_ZN13EditorManager16GetBuiltinEditorEP10EditorBase");
    fn_GetControl = (cbEditor_GetControl_fn)dlsym(RTLD_DEFAULT, "_ZNK8cbEditor10GetControlEv");
    fn_SendMsg = (wxScintilla_SendMsg_fn)dlsym(RTLD_DEFAULT, "_ZNK11wxScintilla7SendMsgEiml");

    fn_cbC2U = (cbC2U_fn)dlsym(RTLD_DEFAULT, "_Z5cbC2UPKc");
    fn_LoadProject = (ProjectManager_LoadProject_fn)dlsym(RTLD_DEFAULT, "_ZN14ProjectManager11LoadProjectERK8wxStringb");
    fn_CloseAllProjects = (ProjectManager_CloseAllProjects_fn)dlsym(RTLD_DEFAULT, "_ZN14ProjectManager16CloseAllProjectsEb");
    fn_AddFileToProject = (ProjectManager_AddFileToProject_fn)dlsym(RTLD_DEFAULT, "_ZN14ProjectManager16AddFileToProjectERK8wxStringP9cbProjecti");
    fn_GetWorkspace = (ProjectManager_GetWorkspace_fn)dlsym(RTLD_DEFAULT, "_ZN14ProjectManager12GetWorkspaceEv");
    fn_ProjSetModified = (cbProject_SetModified_fn)dlsym(RTLD_DEFAULT, "_ZN9cbProject11SetModifiedEb");
    fn_WsSetModified = (cbWorkspace_SetModified_fn)dlsym(RTLD_DEFAULT, "_ZN11cbWorkspace11SetModifiedEb");

    fn_wxGetMousePos = (wxGetMousePosition_fn)dlsym(RTLD_DEFAULT, "_Z18wxGetMousePositionPiS_");
    fn_ScreenToClient = (wxWindow_DoScreenToClient_fn)dlsym(RTLD_DEFAULT, "_ZNK8wxWindow16DoScreenToClientEPiS0_");
}

static void* get_active_editor_base(void) {
    resolve_cb_symbols();
    if (!fn_Manager_Get || !fn_GetEditorManager || !fn_GetActiveEditor) return NULL;
    void *mgr = fn_Manager_Get();
    if (!mgr) return NULL;
    void *emgr = fn_GetEditorManager(mgr);
    if (!emgr) return NULL;
    return fn_GetActiveEditor(emgr);
}

static void* get_active_scintilla(void) {
    void *eb = get_active_editor_base();
    if (!eb || !fn_GetBuiltinEditor || !fn_GetControl) return NULL;
    void *mgr = fn_Manager_Get();
    void *emgr = fn_GetEditorManager(mgr);
    void *cbed = fn_GetBuiltinEditor(emgr, eb);
    if (!cbed) return NULL;
    return fn_GetControl(cbed);
}

static int get_active_filepath(char *out_path, size_t max_len) {
    out_path[0] = '\0';
    void *eb = get_active_editor_base();
    if (!eb) return 0;
    const wchar_t *wpath = *(const wchar_t**)((char*)eb + 0x2f0);
    if (!wpath || !wpath[0]) return 0;
    size_t n = wcstombs(out_path, wpath, max_len - 1);
    if (n == (size_t)-1) {
        out_path[0] = '\0';
        return 0;
    }
    out_path[n] = '\0';
    return (out_path[0] == '/');
}

static int is_source_or_project_file(const char *name) {
    if (!name || name[0] == '.') return 0;
    const char *dot = strrchr(name, '.');
    if (!dot) return 0;
    return (!strcmp(dot, ".c") || !strcmp(dot, ".cpp") || !strcmp(dot, ".cc") ||
            !strcmp(dot, ".cxx") || !strcmp(dot, ".h") || !strcmp(dot, ".hpp") ||
            !strcmp(dot, ".py") || !strcmp(dot, ".txt") || !strcmp(dot, ".md"));
}

static int was_file_seen(const char *name) {
    for (int i = 0; i < g_seen_count; i++) {
        if (strcmp(g_seen_files[i], name) == 0) return 1;
    }
    return 0;
}

static void sync_folder_workspace(const char *active_file) {
    if (!active_file || active_file[0] != '/') return;

    // Write /tmp/.cb_active_file so CPH Judge & Single-File Debugger track active tab
    FILE *af = fopen("/tmp/.cb_active_file", "w");
    if (af) {
        fprintf(af, "%s\n", active_file);
        fclose(af);
    }

    if (!fn_GetProjectManager || !fn_cbC2U || !fn_LoadProject) return;
    void *mgr = fn_Manager_Get();
    if (!mgr) return;
    void *pmgr = fn_GetProjectManager(mgr);
    if (!pmgr) return;

    char dir[1024];
    strncpy(dir, active_file, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = '\0';
    char *slash = strrchr(dir, '/');
    if (!slash || slash == dir) return;
    *slash = '\0';

    const char *folder_name = strrchr(dir, '/');
    folder_name = folder_name ? (folder_name + 1) : dir;

    void *active_proj = *(void**)((char*)pmgr + 0x68);
    void *ui = *(void**)((char*)pmgr + 0x60);

    // Case 1: New folder or no active project loaded yet -> build .vscode_workspace.cbp & load it
    if (strcmp(g_workspace_dir, dir) != 0 || !active_proj) {
        DIR *dp = opendir(dir);
        if (!dp) return;

        char cbp_path[1024];
        snprintf(cbp_path, sizeof(cbp_path), "%s/.vscode_workspace.cbp", dir);
        FILE *fp = fopen(cbp_path, "w");
        if (!fp) {
            closedir(dp);
            return;
        }

        fprintf(fp, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\" ?>\n");
        fprintf(fp, "<CodeBlocks_project_file>\n");
        fprintf(fp, "\t<FileVersion major=\"1\" minor=\"6\" />\n");
        fprintf(fp, "\t<Project>\n");
        fprintf(fp, "\t\t<Option title=\"%s\" />\n", folder_name);
        fprintf(fp, "\t\t<Option pch_mode=\"2\" />\n");
        fprintf(fp, "\t\t<Option compiler=\"gcc\" />\n");
        fprintf(fp, "\t\t<Build>\n");
        fprintf(fp, "\t\t\t<Target title=\"Debug\">\n");
        fprintf(fp, "\t\t\t\t<Option output=\"bin/Debug/app\" prefix_auto=\"1\" extension_auto=\"1\" />\n");
        fprintf(fp, "\t\t\t\t<Option type=\"1\" />\n");
        fprintf(fp, "\t\t\t\t<Option compiler=\"gcc\" />\n");
        fprintf(fp, "\t\t\t</Target>\n");
        fprintf(fp, "\t\t</Build>\n");

        g_seen_count = 0;
        struct dirent *de;
        while ((de = readdir(dp)) != NULL) {
            if (is_source_or_project_file(de->d_name)) {
                fprintf(fp, "\t\t<Unit filename=\"%s\" />\n", de->d_name);
                if (g_seen_count < 256) {
                    strncpy(g_seen_files[g_seen_count], de->d_name, 255);
                    g_seen_files[g_seen_count][255] = '\0';
                    g_seen_count++;
                }
            }
        }
        closedir(dp);

        fprintf(fp, "\t</Project>\n");
        fprintf(fp, "</CodeBlocks_project_file>\n");
        fclose(fp);

        strncpy(g_workspace_dir, dir, sizeof(g_workspace_dir) - 1);

        unsigned char wxstr[64];
        memset(wxstr, 0, sizeof(wxstr));
        fn_cbC2U(wxstr, cbp_path);
        void *proj = fn_LoadProject(pmgr, wxstr, 1);
        if (proj && fn_ProjSetModified) fn_ProjSetModified(proj, 0);
        if (fn_GetWorkspace && fn_WsSetModified) {
            void *ws = fn_GetWorkspace(pmgr);
            if (ws) fn_WsSetModified(ws, 0);
        }
        if (ui) {
            void (**vtable)(void*) = *(void (***)(void*))ui;
            if (vtable && vtable[4]) vtable[4](ui); // m_ui->RebuildTree() at offset 0x20
        }
        return;
    }

    // Case 2: Same folder, check if user saved a brand-new file into this folder
    DIR *dp = opendir(dir);
    if (!dp) return;
    int added_any = 0;
    struct dirent *de;
    while ((de = readdir(dp)) != NULL) {
        if (is_source_or_project_file(de->d_name) && !was_file_seen(de->d_name)) {
            if (g_seen_count < 256) {
                strncpy(g_seen_files[g_seen_count], de->d_name, 255);
                g_seen_files[g_seen_count][255] = '\0';
                g_seen_count++;
            }
            if (fn_AddFileToProject && active_proj) {
                char full_p[1024];
                snprintf(full_p, sizeof(full_p), "%s/%s", dir, de->d_name);
                unsigned char wxstr[64];
                memset(wxstr, 0, sizeof(wxstr));
                fn_cbC2U(wxstr, full_p);
                fn_AddFileToProject(pmgr, wxstr, active_proj, 0);
                added_any = 1;
            }
        }
    }
    closedir(dp);

    if (added_any) {
        if (active_proj && fn_ProjSetModified) fn_ProjSetModified(active_proj, 0);
        if (fn_GetWorkspace && fn_WsSetModified) {
            void *ws = fn_GetWorkspace(pmgr);
            if (ws) fn_WsSetModified(ws, 0);
        }
        if (ui) {
            void (**vtable)(void*) = *(void (***)(void*))ui;
            if (vtable && vtable[4]) vtable[4](ui);
        }
    }
}

static uint64_t hash_str(const char *s, size_t len) {
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < len; i++) {
        h ^= (unsigned char)s[i];
        h *= 1099511628211ULL;
    }
    return h;
}

static void apply_scintilla_opacity(void *sci) {
    if (!sci || !fn_SendMsg) return;

    // 0. Dark mode for Untitled / Plain text editors (fixes white background & yellow active line!)
    fn_SendMsg(sci, SCI_STYLESETBACK, 32, 0x1E1E1E); // STYLE_DEFAULT back #1E1E1E
    fn_SendMsg(sci, SCI_STYLESETFORE, 32, 0xD4D4D4); // STYLE_DEFAULT fore #D4D4D4
    if (fn_SendMsg(sci, SCI_STYLEGETBACK, 0, 0) == 0xFFFFFF) {
        fn_SendMsg(sci, SCI_STYLESETBACK, 0, 0x1E1E1E);
        fn_SendMsg(sci, SCI_STYLESETFORE, 0, 0xD4D4D4);
    }
    fn_SendMsg(sci, SCI_STYLESETBACK, 33, 0x1E1E1E); // Line numbers margin #1E1E1E
    fn_SendMsg(sci, SCI_STYLESETFORE, 33, 0x858585); // Line numbers text #858585
    fn_SendMsg(sci, SCI_SETCARETLINEBACK, 0x282828, 0); // Subtle dark active line #282828 (no yellow!)
    fn_SendMsg(sci, SCI_SETCARETFORE, 0xADAFEA, 0);     // Caret color #AEAFAD
    fn_SendMsg(sci, SCI_SETFOLDMARGINCOLOUR, 1, 0x1E1E1E);
    fn_SendMsg(sci, SCI_SETFOLDMARGINHICOLOUR, 1, 0x1E1E1E);

    // 1. 50% Opacity Highlights (Selection & Matching Braces)
    fn_SendMsg(sci, SCI_STYLESETBACK, 34, 0x58462A); // #2A4658 (50% steel-blue)
    fn_SendMsg(sci, SCI_STYLESETFORE, 34, 0xDCDCDC); // #DCDCDC
    fn_SendMsg(sci, SCI_STYLESETBOLD, 34, 0);
    fn_SendMsg(sci, SCI_SETSELALPHA, 128, 0);        // 50% selection transparency

    // 2. VS Code typography: +2px top/bottom line padding (1.35x line height) + LCD subpixel AA + buffered draw
    fn_SendMsg(sci, SCI_SETEXTRAASCENT, 2, 0);
    fn_SendMsg(sci, SCI_SETEXTRADESCENT, 2, 0);
    fn_SendMsg(sci, SCI_SETFONTQUALITY, 3, 0);
    fn_SendMsg(sci, SCI_SETBUFFEREDDRAW, 1, 0);
    fn_SendMsg(sci, SCI_INDICSETALPHA, 8, 50);
    fn_SendMsg(sci, SCI_INDICSETOUTLINEALPHA, 8, 85);

    // 3. Configure VS Code LSP Squiggle Indicators
    fn_SendMsg(sci, SCI_INDICSETSTYLE, INDIC_LSP_ERROR, INDIC_SQUIGGLE);
    fn_SendMsg(sci, SCI_INDICSETFORE,  INDIC_LSP_ERROR, 0x4747F4); // #F44747 VS Code Error Red
    fn_SendMsg(sci, SCI_INDICSETSTYLE, INDIC_LSP_WARN,  INDIC_SQUIGGLE);
    fn_SendMsg(sci, SCI_INDICSETFORE,  INDIC_LSP_WARN,  0x00A7CC); // #CCA700 VS Code Warning Yellow

    // 4. Configure VS Code Error Lens Annotation Styles
    fn_SendMsg(sci, SCI_STYLESETBACK, STYLE_LSP_ERROR, 0x201D3A);  // #3A1D20 muted dark red box
    fn_SendMsg(sci, SCI_STYLESETFORE, STYLE_LSP_ERROR, 0x8080F4);  // #F48080 soft red text
    fn_SendMsg(sci, SCI_STYLESETITALIC, STYLE_LSP_ERROR, 1);

    fn_SendMsg(sci, SCI_STYLESETBACK, STYLE_LSP_WARN,  0x182C38);  // #382C18 muted dark amber box
    fn_SendMsg(sci, SCI_STYLESETFORE, STYLE_LSP_WARN,  0x60C8F0);  // #F0C860 soft amber text
    fn_SendMsg(sci, SCI_STYLESETITALIC, STYLE_LSP_WARN, 1);
}

static void apply_diagnostics(void *sci) {
    if (!sci || !fn_SendMsg) return;
    long doc_len = fn_SendMsg(sci, SCI_GETLENGTH, 0, 0);
    if (doc_len <= 0) return;

    fn_SendMsg(sci, SCI_SETINDICATORCURRENT, INDIC_LSP_ERROR, 0);
    fn_SendMsg(sci, SCI_INDICATORCLEARRANGE, 0, doc_len);
    fn_SendMsg(sci, SCI_SETINDICATORCURRENT, INDIC_LSP_WARN, 0);
    fn_SendMsg(sci, SCI_INDICATORCLEARRANGE, 0, doc_len);
    fn_SendMsg(sci, SCI_ANNOTATIONCLEARALL, 0, 0);

    pthread_mutex_lock(&g_mutex);
    for (int i = 0; i < g_diag_count; i++) {
        int line = g_diags[i].line;
        long start = fn_SendMsg(sci, SCI_POSITIONFROMLINE, line, 0);
        long end   = fn_SendMsg(sci, SCI_GETLINEENDPOSITION, line, 0);
        if (start < 0 || end <= start) continue;

        char *buf = (char*)malloc(doc_len + 2);
        if (buf) {
            fn_SendMsg(sci, SCI_GETTEXT, doc_len + 1, (long)buf);
            while (start < end && (buf[start] == ' ' || buf[start] == '\t')) start++;
            free(buf);
        }

        int indic = g_diags[i].is_err ? INDIC_LSP_ERROR : INDIC_LSP_WARN;
        fn_SendMsg(sci, SCI_SETINDICATORCURRENT, indic, 0);
        fn_SendMsg(sci, SCI_INDICATORFILLRANGE, start, end - start);

        // Warnings (e.g., unused variable / uninitialized) ONLY show yellow squiggle below;
        // full message appears on mouse hover (or if Ctrl+Shift+M forces inline boxes).
        if (g_diags[i].is_err || g_show_annotations) {
            fn_SendMsg(sci, SCI_ANNOTATIONSETTEXT, line, (long)g_diags[i].msg);
            fn_SendMsg(sci, SCI_ANNOTATIONSETSTYLE, line, g_diags[i].is_err ? STYLE_LSP_ERROR : STYLE_LSP_WARN);
        }
    }
    fn_SendMsg(sci, SCI_ANNOTATIONSETVISIBLE, ANNOTATION_BOXED, 0);
    pthread_mutex_unlock(&g_mutex);
}

static void* run_compiler_thread(void *arg) {
    char *code = (char*)arg;
    const char *tmp_src = "/tmp/cb_lsp_live.cpp";
    FILE *fp = fopen(tmp_src, "w");
    if (!fp) {
        free(code);
        g_checker_busy = 0;
        return NULL;
    }
    fputs(code, fp);
    fclose(fp);
    free(code);

    FILE *pipe = popen("g++ -fsyntax-only -Wall -Wextra -Wformat=2 -fdiagnostics-color=never /tmp/cb_lsp_live.cpp 2>&1", "r");
    if (!pipe) {
        g_checker_busy = 0;
        return NULL;
    }

    DiagItem local_diags[64];
    int count = 0;
    char line_buf[512];
    while (fgets(line_buf, sizeof(line_buf), pipe) && count < 64) {
        if (strncmp(line_buf, "/tmp/cb_lsp_live.cpp:", 21) != 0) continue;
        char *p = line_buf + 21;
        int line_num = atoi(p);
        if (line_num <= 0) continue;
        p = strchr(p, ':');
        if (!p) continue;
        p++;
        if (*p >= '0' && *p <= '9') {
            p = strchr(p, ':');
            if (!p) continue;
            p++;
        }
        while (*p == ' ') p++;

        int is_err = 0;
        const char *msg_start = NULL;
        if (strncmp(p, "error:", 6) == 0 || strncmp(p, "fatal error:", 12) == 0) {
            is_err = 1;
            msg_start = strchr(p, ':') + 1;
        } else if (strncmp(p, "warning:", 8) == 0) {
            is_err = 0;
            msg_start = p + 8;
        } else {
            continue;
        }
        while (*msg_start == ' ') msg_start++;
        char *nl = strpbrk((char*)msg_start, "\r\n");
        if (nl) *nl = '\0';

        int merged = 0;
        for (int j = 0; j < count; j++) {
            if (local_diags[j].line == line_num - 1) {
                if (is_err) local_diags[j].is_err = 1;
                merged = 1;
                break;
            }
        }
        if (!merged) {
            local_diags[count].line = line_num - 1;
            local_diags[count].is_err = is_err;
            snprintf(local_diags[count].msg, sizeof(local_diags[count].msg),
                     "  %s %s  ", is_err ? "\xE2\x9B\x94 [Error]" : "\xE2\x9A\xA0 [Warning]", msg_start);
            count++;
        }
    }
    pclose(pipe);

    pthread_mutex_lock(&g_mutex);
    memcpy(g_diags, local_diags, sizeof(DiagItem) * count);
    g_diag_count = count;
    pthread_mutex_unlock(&g_mutex);

    g_checker_busy = 0;
    return NULL;
}

// Fast 150ms hover check: shows warning/error tooltip only when user hovers over the squiggle
static int hover_tick(void *data) {
    (void)data;
    void *sci = get_active_scintilla();
    if (!sci || !fn_SendMsg || !fn_wxGetMousePos || !fn_ScreenToClient) return 1;

    int mx = 0, my = 0;
    fn_wxGetMousePos(&mx, &my);
    fn_ScreenToClient(sci, &mx, &my);

    long pos = fn_SendMsg(sci, SCI_POSITIONFROMPOINTCLOSE, (unsigned long)mx, (long)my);
    if (pos >= 0) {
        long line = fn_SendMsg(sci, SCI_LINEFROMPOSITION, (unsigned long)pos, 0);
        pthread_mutex_lock(&g_mutex);
        int found = -1;
        for (int i = 0; i < g_diag_count; i++) {
            if (g_diags[i].line == (int)line) {
                found = i;
                break;
            }
        }
        if (found >= 0) {
            if (!g_hover_calltip_active || g_last_hover_line != (int)line) {
                fn_SendMsg(sci, SCI_CALLTIPSETBACK, 0x262525, 0); // #252526 VS Code hover background
                fn_SendMsg(sci, SCI_CALLTIPSETFORE, g_diags[found].is_err ? 0x8080F4 : 0x60C8F0, 0);
                fn_SendMsg(sci, SCI_CALLTIPSHOW, (unsigned long)pos, (long)g_diags[found].msg);
                g_hover_calltip_active = 1;
                g_last_hover_line = (int)line;
            }
            pthread_mutex_unlock(&g_mutex);
            return 1;
        }
        pthread_mutex_unlock(&g_mutex);
    }

    if (g_hover_calltip_active) {
        fn_SendMsg(sci, SCI_CALLTIPCANCEL, 0, 0);
        g_hover_calltip_active = 0;
        g_last_hover_line = -1;
    }
    return 1;
}

static int periodic_tick(void *data) {
    (void)data;
    void *sci = get_active_scintilla();
    if (!sci || !fn_SendMsg) return 1;

    apply_scintilla_opacity(sci);

    // Sync active file & auto-detect folder workspace on save/open
    char active_file[1024];
    if (get_active_filepath(active_file, sizeof(active_file))) {
        sync_folder_workspace(active_file);
    }

    long len = fn_SendMsg(sci, SCI_GETLENGTH, 0, 0);
    if (len <= 0 || len > 250000) return 1;

    char *buf = (char*)malloc(len + 2);
    if (!buf) return 1;
    fn_SendMsg(sci, SCI_GETTEXT, len + 1, (long)buf);
    uint64_t h = hash_str(buf, len);

    if ((h != g_last_hash || sci != g_last_sci) && !g_checker_busy) {
        g_last_hash = h;
        g_last_sci = sci;
        g_checker_busy = 1;
        pthread_t tid;
        if (pthread_create(&tid, NULL, run_compiler_thread, buf) == 0) {
            pthread_detach(tid);
        } else {
            free(buf);
            g_checker_busy = 0;
        }
    } else {
        free(buf);
        apply_diagnostics(sci);
    }
    return 1;
}

static void launch_debugger(int asan) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "$HOME/.local/bin/cb_single_file_debug.sh %s &", asan ? "--asan" : "");
    int r = system(cmd);
    (void)r;
}

static void launch_cph_judge(void) {
    // Touch trigger file so if CPH Judge is already open it re-runs immediately; otherwise launch it
    FILE *tf = fopen("/tmp/.cb_cph_run_trigger", "w");
    if (tf) {
        fprintf(tf, "%ld\n", (long)time(NULL));
        fclose(tf);
    }
    int r = system("pgrep -f cb_cph_judge.py >/dev/null 2>&1 || ($HOME/.local/bin/cb_cph_judge.py --run >/dev/null 2>&1 &)");
    (void)r;
}

static int my_key_snooper(void *grab_widget, GdkEventKey_t *event, void *func_data) {
    (void)grab_widget;
    (void)func_data;
    if (!event) return 0;

    // F5 (0xffbe) = Single-File Visual GDB Debugger, Ctrl+F5 = AddressSanitizer
    if (event->type == 8 && event->keyval == 0xffbe) {
        int ctrl = (event->state & 4) != 0;
        launch_debugger(ctrl);
        return 1;
    }

    // F6 (0xffbf) or Ctrl+Alt+B = CPH Judge: Run All Testcases
    if (event->type == 8 && (event->keyval == 0xffbf ||
        ((event->state & 4) && (event->state & 8) && (event->keyval == 'b' || event->keyval == 'B')))) {
        launch_cph_judge();
        return 1;
    }

    // Ctrl+Shift+M = Toggle inline warning Error Lens boxes
    if (event->type == 8 && (event->state & 4) && (event->state & 1) &&
        (event->keyval == 'm' || event->keyval == 'M')) {
        g_show_annotations = !g_show_annotations;
        void *sci = get_active_scintilla();
        if (sci) apply_diagnostics(sci);
        return 1;
    }

    // Ctrl + I / J / K / L cursor navigation
    if (event->state & 4) {
        uint32_t new_keyval = 0;
        uint16_t new_hwcode = 0;

        switch (event->keyval) {
            case 'i': case 'I': new_keyval = 0xff52; new_hwcode = 111; break; // Up
            case 'k': case 'K': new_keyval = 0xff54; new_hwcode = 116; break; // Down
            case 'j': case 'J': new_keyval = 0xff51; new_hwcode = 113; break; // Left
            case 'l': case 'L': new_keyval = 0xff53; new_hwcode = 114; break; // Right
            default: break;
        }

        if (new_keyval != 0) {
            event->keyval = new_keyval;
            event->hardware_keycode = new_hwcode;
            if (event->state & 8) {
                event->state &= ~8; // Ctrl+Alt+J/L -> Word Jump (Ctrl+Left/Right)
            } else {
                event->state &= ~4; // Ctrl+I/J/K/L -> Arrow key
            }
            if (event->string && event->length > 0) {
                event->string[0] = '\0';
                event->length = 0;
            }
            return 0;
        }
    }
    return 0;
}

__attribute__((constructor))
static void init_vscode_helper(void) {
    gtk_key_snooper_install_fn install_snooper =
        (gtk_key_snooper_install_fn)dlsym(RTLD_DEFAULT, "gtk_key_snooper_install");
    if (install_snooper) {
        install_snooper(my_key_snooper, NULL);
    }

    g_timeout_add_fn timeout_add =
        (g_timeout_add_fn)dlsym(RTLD_DEFAULT, "g_timeout_add");
    if (timeout_add) {
        timeout_add(650, periodic_tick, NULL);
        timeout_add(150, hover_tick, NULL);
    }
}
