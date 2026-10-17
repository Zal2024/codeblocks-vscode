#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>

// Code::Blocks & wxScintilla function pointers
typedef void* (*Manager_Get_fn)(void);
typedef void* (*Manager_GetAppWindow_fn)(void *mgr);
typedef int   (*Manager_IsAppShuttingDown_fn)(void);
typedef void* (*Manager_GetEditorManager_fn)(void *mgr);
typedef void* (*Manager_GetProjectManager_fn)(void *mgr);
typedef void* (*EditorManager_GetActiveEditor_fn)(void *emgr);
typedef void* (*EditorManager_GetBuiltinEditor_fn)(void *emgr, void *ebase);
typedef const void* (*EditorBase_GetFilename_fn)(const void *ebase);
typedef void* (*cbEditor_GetControl_fn)(void *cbed);
typedef long long (*wxScintilla_SendMsg_fn)(void *sci, unsigned int msg, unsigned long long wp, long long lp);

// ProjectManager & wxString helpers
typedef void* (*cbC2U_fn)(void *wxstr_out, const char *utf8);
typedef void* (*ProjectManager_GetActiveProject_fn)(void *pmgr);
typedef void* (*ProjectManager_LoadProject_fn)(void *pmgr, const void *wxstr_path, int activateIt);
typedef void* (*ProjectManager_AddFileToProject_fn)(void *pmgr, const void *wxstr_path, void *proj, int target);
typedef void* (*ProjectManager_GetWorkspace_fn)(void *pmgr);
typedef void  (*cbProject_SetModified_fn)(void *proj, int modified);
typedef void  (*cbWorkspace_SetModified_fn)(void *ws, int modified);

// Mouse hover helpers
typedef void  (*wxGetMousePosition_fn)(int *x, int *y);
typedef void  (*wxWindow_DoScreenToClient_fn)(void *win, int *x, int *y);

static Manager_Get_fn fn_Manager_Get = NULL;
static Manager_GetAppWindow_fn fn_GetAppWindow = NULL;
static Manager_IsAppShuttingDown_fn fn_IsAppShuttingDown = NULL;
static Manager_GetEditorManager_fn fn_GetEditorManager = NULL;
static Manager_GetProjectManager_fn fn_GetProjectManager = NULL;
static EditorManager_GetActiveEditor_fn fn_GetActiveEditor = NULL;
static EditorManager_GetBuiltinEditor_fn fn_GetBuiltinEditor = NULL;
static EditorBase_GetFilename_fn fn_GetFilename = NULL;
static cbEditor_GetControl_fn fn_GetControl = NULL;
static wxScintilla_SendMsg_fn fn_SendMsg = NULL;

static cbC2U_fn fn_cbC2U = NULL;
static ProjectManager_GetActiveProject_fn fn_GetActiveProject = NULL;
static ProjectManager_LoadProject_fn fn_LoadProject = NULL;
static ProjectManager_AddFileToProject_fn fn_AddFileToProject = NULL;
static ProjectManager_GetWorkspace_fn fn_GetWorkspace = NULL;
static cbProject_SetModified_fn fn_ProjSetModified = NULL;
static cbWorkspace_SetModified_fn fn_WsSetModified = NULL;
static wxGetMousePosition_fn fn_wxGetMousePos = NULL;
static wxWindow_DoScreenToClient_fn fn_ScreenToClient = NULL;

static int g_startup_ready = 0;
static int g_tick_counter = 0;
static DWORD g_main_thread_id = 0;
static HHOOK g_getmsg_hook = NULL;
static HHOOK g_kbd_hook = NULL;
static volatile LONG g_timer_installed = 0;

static void dbg_log(const char *fmt, ...) {
    char temp_dir[MAX_PATH];
    if (!GetTempPathA(sizeof(temp_dir), temp_dir)) return;
    char log_path[MAX_PATH];
    snprintf(log_path, sizeof(log_path), "%scb_vscode_helper.log", temp_dir);
    FILE *fp = fopen(log_path, "a");
    if (!fp) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(fp, fmt, ap);
    fprintf(fp, "\n");
    va_end(ap);
    fclose(fp);
}

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
#define SCI_AUTOCACTIVE             2102
#define SCI_GETLINEENDPOSITION      2136
#define SCI_LINEFROMPOSITION        2166
#define SCI_POSITIONFROMLINE        2167
#define SCI_GETTEXT                 2182
#define SCI_CALLTIPSHOW             2200
#define SCI_CALLTIPCANCEL           2201
#define SCI_CALLTIPACTIVE           2202
#define SCI_CALLTIPSETBACK          2205
#define SCI_CALLTIPSETFORE          2206
#define SCI_SETFOLDMARGINCOLOUR     2290
#define SCI_SETFOLDMARGINHICOLOUR   2291
#define SCI_LINEDOWN                2300
#define SCI_LINEDOWNEXTEND          2301
#define SCI_LINEUP                  2302
#define SCI_LINEUPEXTEND            2303
#define SCI_CHARLEFT                2304
#define SCI_CHARLEFTEXTEND          2305
#define SCI_CHARRIGHT               2306
#define SCI_CHARRIGHTEXTEND         2307
#define SCI_WORDLEFT                2308
#define SCI_WORDLEFTEXTEND          2309
#define SCI_WORDRIGHT               2310
#define SCI_WORDRIGHTEXTEND         2311
#define SCI_SETSELALPHA             2478
#define SCI_STYLEGETBACK            2482
#define SCI_SETINDICATORCURRENT     2500
#define SCI_INDICATORFILLRANGE      2504
#define SCI_INDICATORCLEARRANGE     2505
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
static int g_show_annotations = 0;
static volatile LONG g_checker_busy = 0;
static int g_hover_calltip_active = 0;
static int g_last_hover_line = -1;
static CRITICAL_SECTION g_cs;

// Auto-workspace tracking
static char g_workspace_dir[MAX_PATH] = "";
static char g_seen_files[256][MAX_PATH];
static int g_seen_count = 0;

static void resolve_cb_symbols(void) {
    if (fn_Manager_Get) return;

    HMODULE hCB = GetModuleHandleW(L"codeblocks.dll");
    if (!hCB) return;
    HMODULE hWX = GetModuleHandleW(L"wxmsw32u_gcc_custom.dll");

    fn_Manager_Get = (Manager_Get_fn)GetProcAddress(hCB, "_ZN7Manager3GetEv");
    fn_GetAppWindow = (Manager_GetAppWindow_fn)GetProcAddress(hCB, "_ZNK7Manager12GetAppWindowEv");
    fn_IsAppShuttingDown = (Manager_IsAppShuttingDown_fn)GetProcAddress(hCB, "_ZN7Manager17IsAppShuttingDownEv");
    fn_GetEditorManager = (Manager_GetEditorManager_fn)GetProcAddress(hCB, "_ZNK7Manager16GetEditorManagerEv");
    fn_GetProjectManager = (Manager_GetProjectManager_fn)GetProcAddress(hCB, "_ZNK7Manager17GetProjectManagerEv");
    fn_GetActiveEditor = (EditorManager_GetActiveEditor_fn)GetProcAddress(hCB, "_ZN13EditorManager15GetActiveEditorEv");
    fn_GetBuiltinEditor = (EditorManager_GetBuiltinEditor_fn)GetProcAddress(hCB, "_ZN13EditorManager16GetBuiltinEditorEP10EditorBase");
    fn_GetFilename = (EditorBase_GetFilename_fn)GetProcAddress(hCB, "_ZNK10EditorBase11GetFilenameEv");
    fn_GetControl = (cbEditor_GetControl_fn)GetProcAddress(hCB, "_ZNK8cbEditor10GetControlEv");
    fn_SendMsg = (wxScintilla_SendMsg_fn)GetProcAddress(hCB, "_ZNK11wxScintilla7SendMsgEiyx");

    fn_cbC2U = (cbC2U_fn)GetProcAddress(hCB, "_Z5cbC2UPKc");
    fn_GetActiveProject = (ProjectManager_GetActiveProject_fn)GetProcAddress(hCB, "_ZNK14ProjectManager16GetActiveProjectEv");
    if (!fn_GetActiveProject)
        fn_GetActiveProject = (ProjectManager_GetActiveProject_fn)GetProcAddress(hCB, "_ZN14ProjectManager16GetActiveProjectEv");
    fn_LoadProject = (ProjectManager_LoadProject_fn)GetProcAddress(hCB, "_ZN14ProjectManager11LoadProjectERK8wxStringb");
    fn_GetWorkspace = (ProjectManager_GetWorkspace_fn)GetProcAddress(hCB, "_ZN14ProjectManager12GetWorkspaceEv");
    fn_ProjSetModified = (cbProject_SetModified_fn)GetProcAddress(hCB, "_ZN9cbProject11SetModifiedEb");
    fn_WsSetModified = (cbWorkspace_SetModified_fn)GetProcAddress(hCB, "_ZN11cbWorkspace11SetModifiedEb");
    fn_AddFileToProject = (ProjectManager_AddFileToProject_fn)GetProcAddress(hCB, "_ZN14ProjectManager16AddFileToProjectERK8wxStringP9cbProjecti");

    if (hWX) {
        fn_wxGetMousePos = (wxGetMousePosition_fn)GetProcAddress(hWX, "_Z18wxGetMousePositionPiS_");
        fn_ScreenToClient = (wxWindow_DoScreenToClient_fn)GetProcAddress(hWX, "_ZNK8wxWindow16DoScreenToClientEPiS0_");
    }
    dbg_log("resolve_cb_symbols: Manager_Get=%p GetFilename=%p GetActiveProject=%p LoadProject=%p",
            (void*)fn_Manager_Get, (void*)fn_GetFilename, (void*)fn_GetActiveProject, (void*)fn_LoadProject);
}

static void* get_active_editor_base(void) {
    if (!g_startup_ready) return NULL;
    resolve_cb_symbols();
    if (!fn_Manager_Get || !fn_GetAppWindow || !fn_IsAppShuttingDown || !fn_GetEditorManager || !fn_GetActiveEditor) return NULL;
    if (fn_IsAppShuttingDown()) return NULL;
    void *mgr = fn_Manager_Get();
    if (!mgr || !fn_GetAppWindow(mgr)) return NULL;
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
    // EditorBase::m_Filename is at offset 0x2a0 in 64-bit Code::Blocks (wxString / std::wstring)
    const wchar_t *wpath = *(const wchar_t**)((const char*)eb + 0x2a0);
    if (!wpath || !wpath[0]) return 0;
    int n = WideCharToMultiByte(CP_UTF8, 0, wpath, -1, out_path, (int)max_len, NULL, NULL);
    if (n <= 0) {
        out_path[0] = '\0';
        return 0;
    }
    return (out_path[0] != '\0');
}

static int is_source_or_project_file(const char *name) {
    if (!name || name[0] == '.') return 0;
    const char *dot = strrchr(name, '.');
    if (!dot) return 0;
    return (!_stricmp(dot, ".c") || !_stricmp(dot, ".cpp") || !_stricmp(dot, ".cc") ||
            !_stricmp(dot, ".cxx") || !_stricmp(dot, ".h") || !_stricmp(dot, ".hpp") ||
            !_stricmp(dot, ".py") || !_stricmp(dot, ".txt") || !_stricmp(dot, ".md"));
}

static int was_file_seen(const char *name) {
    for (int i = 0; i < g_seen_count; i++) {
        if (_stricmp(g_seen_files[i], name) == 0) return 1;
    }
    return 0;
}

static void sync_folder_workspace(const char *active_file, int save_active_tracker) {
    if (!active_file || !active_file[0]) return;

    if (save_active_tracker) {
        char temp_dir[MAX_PATH];
        if (GetTempPathA(sizeof(temp_dir), temp_dir)) {
            char act_file_path[MAX_PATH];
            snprintf(act_file_path, sizeof(act_file_path), "%scb_active_file.txt", temp_dir);
            FILE *af = fopen(act_file_path, "w");
            if (af) {
                fprintf(af, "%s\n", active_file);
                fclose(af);
            }
        }
    }

    if (!fn_GetProjectManager || !fn_cbC2U || !fn_LoadProject) return;
    void *mgr = fn_Manager_Get();
    if (!mgr) return;
    void *pmgr = fn_GetProjectManager(mgr);
    if (!pmgr) return;

    char dir[MAX_PATH];
    strncpy(dir, active_file, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = '\0';
    char *slash = strrchr(dir, '\\');
    char *fwd_slash = strrchr(dir, '/');
    if (fwd_slash && fwd_slash > slash) slash = fwd_slash;
    if (!slash || slash == dir) return;
    *slash = '\0';

    const char *folder_name = strrchr(dir, '\\');
    const char *fwd_f = strrchr(dir, '/');
    if (fwd_f && fwd_f > folder_name) folder_name = fwd_f;
    folder_name = folder_name ? (folder_name + 1) : dir;

    void *active_proj = fn_GetActiveProject ? fn_GetActiveProject(pmgr) : NULL;

    // Case 1: Folder changed or no active project loaded
    if (_stricmp(g_workspace_dir, dir) != 0 || !active_proj) {
        char cbp_path[MAX_PATH];
        snprintf(cbp_path, sizeof(cbp_path), "%s\\.vscode_workspace.cbp", dir);
        FILE *fp = fopen(cbp_path, "w");
        if (!fp) return;

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
        char search_path[MAX_PATH];
        snprintf(search_path, sizeof(search_path), "%s\\*.*", dir);
        WIN32_FIND_DATAA fd;
        HANDLE hFind = FindFirstFileA(search_path, &fd);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && is_source_or_project_file(fd.cFileName)) {
                    fprintf(fp, "\t\t<Unit filename=\"%s\" />\n", fd.cFileName);
                    if (g_seen_count < 256) {
                        strncpy(g_seen_files[g_seen_count], fd.cFileName, MAX_PATH - 1);
                        g_seen_files[g_seen_count][MAX_PATH - 1] = '\0';
                        g_seen_count++;
                    }
                }
            } while (FindNextFileA(hFind, &fd));
            FindClose(hFind);
        }

        fprintf(fp, "\t</Project>\n");
        fprintf(fp, "</CodeBlocks_project_file>\n");
        fclose(fp);

        strncpy(g_workspace_dir, dir, sizeof(g_workspace_dir) - 1);

        dbg_log("sync_folder_workspace: loading project %s", cbp_path);
        unsigned char wxstr[64];
        memset(wxstr, 0, sizeof(wxstr));
        fn_cbC2U(wxstr, cbp_path);
        void *proj = fn_LoadProject(pmgr, wxstr, 1);
        if (proj && fn_ProjSetModified) fn_ProjSetModified(proj, 0);
        if (fn_GetWorkspace && fn_WsSetModified) {
            void *ws = fn_GetWorkspace(pmgr);
            if (ws) fn_WsSetModified(ws, 0);
        }
        return;
    }

    // Case 2: Same folder, detect newly added files
    char search_path[MAX_PATH];
    snprintf(search_path, sizeof(search_path), "%s\\*.*", dir);
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(search_path, &fd);
    int added_any = 0;
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                is_source_or_project_file(fd.cFileName) &&
                !was_file_seen(fd.cFileName)) {
                if (g_seen_count < 256) {
                    strncpy(g_seen_files[g_seen_count], fd.cFileName, MAX_PATH - 1);
                    g_seen_files[g_seen_count][MAX_PATH - 1] = '\0';
                    g_seen_count++;
                }
                if (fn_AddFileToProject && active_proj) {
                    char full_p[MAX_PATH];
                    snprintf(full_p, sizeof(full_p), "%s\\%s", dir, fd.cFileName);
                    unsigned char wxstr[64];
                    memset(wxstr, 0, sizeof(wxstr));
                    fn_cbC2U(wxstr, full_p);
                    fn_AddFileToProject(pmgr, wxstr, active_proj, 0);
                    added_any = 1;
                }
            }
        } while (FindNextFileA(hFind, &fd));
        FindClose(hFind);
    }

    if (added_any) {
        if (active_proj && fn_ProjSetModified) fn_ProjSetModified(active_proj, 0);
        if (fn_GetWorkspace && fn_WsSetModified) {
            void *ws = fn_GetWorkspace(pmgr);
            if (ws) fn_WsSetModified(ws, 0);
        }
    }
}

static void ensure_warmup_workspace_loaded(void) {
    if (!fn_GetProjectManager || !fn_GetActiveProject) return;
    void *mgr = fn_Manager_Get();
    if (!mgr) return;
    void *pmgr = fn_GetProjectManager(mgr);
    if (!pmgr) return;
    if (fn_GetActiveProject(pmgr) != NULL) return;

    char temp_dir[MAX_PATH];
    if (!GetTempPathA(sizeof(temp_dir), temp_dir)) return;
    char ws_dir[MAX_PATH];
    snprintf(ws_dir, sizeof(ws_dir), "%scb_vscode_workspace", temp_dir);
    CreateDirectoryA(ws_dir, NULL);

    char warmup_cpp[MAX_PATH];
    snprintf(warmup_cpp, sizeof(warmup_cpp), "%s\\vscode_warmup.cpp", ws_dir);
    if (GetFileAttributesA(warmup_cpp) == INVALID_FILE_ATTRIBUTES) {
        FILE *fp = fopen(warmup_cpp, "w");
        if (fp) {
            fprintf(fp, "#include <iostream>\n#include <vector>\n#include <string>\n#include <algorithm>\nint main() { return 0; }\n");
            fclose(fp);
        }
    }
    sync_folder_workspace(warmup_cpp, 0);
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

    // 0. Dark mode for Untitled / Plain text editors & CallTips
    fn_SendMsg(sci, SCI_STYLESETBACK, 32, 0x1E1E1E); // #1E1E1E
    fn_SendMsg(sci, SCI_STYLESETFORE, 32, 0xD4D4D4); // #D4D4D4
    if (fn_SendMsg(sci, SCI_STYLEGETBACK, 0, 0) == 0xFFFFFF) {
        fn_SendMsg(sci, SCI_STYLESETBACK, 0, 0x1E1E1E);
        fn_SendMsg(sci, SCI_STYLESETFORE, 0, 0xD4D4D4);
    }
    fn_SendMsg(sci, SCI_STYLESETBACK, 33, 0x1E1E1E);
    fn_SendMsg(sci, SCI_STYLESETFORE, 33, 0x858585);
    fn_SendMsg(sci, SCI_SETCARETLINEBACK, 0x282828, 0);
    fn_SendMsg(sci, SCI_SETCARETFORE, 0xADAFEA, 0);
    fn_SendMsg(sci, SCI_SETFOLDMARGINCOLOUR, 1, 0x1E1E1E);
    fn_SendMsg(sci, SCI_SETFOLDMARGINHICOLOUR, 1, 0x1E1E1E);
    fn_SendMsg(sci, SCI_CALLTIPSETBACK, 0x262525, 0);
    fn_SendMsg(sci, SCI_CALLTIPSETFORE, 0xD4D4D4, 0);

    // 1. 50% Opacity Highlights (Selection & Braces)
    fn_SendMsg(sci, SCI_STYLESETBACK, 34, 0x58462A);
    fn_SendMsg(sci, SCI_STYLESETFORE, 34, 0xDCDCDC);
    fn_SendMsg(sci, SCI_STYLESETBOLD, 34, 0);
    fn_SendMsg(sci, SCI_SETSELALPHA, 128, 0);

    // 2. VS Code typography: +2px extra ascent/descent, LCD AA, buffered draw
    fn_SendMsg(sci, SCI_SETEXTRAASCENT, 2, 0);
    fn_SendMsg(sci, SCI_SETEXTRADESCENT, 2, 0);
    fn_SendMsg(sci, SCI_SETFONTQUALITY, 3, 0);
    fn_SendMsg(sci, SCI_SETBUFFEREDDRAW, 1, 0);
    fn_SendMsg(sci, SCI_INDICSETALPHA, 8, 50);
    fn_SendMsg(sci, SCI_INDICSETOUTLINEALPHA, 8, 85);

    // 3. LSP Squiggle indicators
    fn_SendMsg(sci, SCI_INDICSETSTYLE, INDIC_LSP_ERROR, INDIC_SQUIGGLE);
    fn_SendMsg(sci, SCI_INDICSETFORE,  INDIC_LSP_ERROR, 0x4747F4);
    fn_SendMsg(sci, SCI_INDICSETSTYLE, INDIC_LSP_WARN,  INDIC_SQUIGGLE);
    fn_SendMsg(sci, SCI_INDICSETFORE,  INDIC_LSP_WARN,  0x00A7CC);

    // 4. Error Lens Annotation Styles
    fn_SendMsg(sci, SCI_STYLESETBACK, STYLE_LSP_ERROR, 0x201D3A);
    fn_SendMsg(sci, SCI_STYLESETFORE, STYLE_LSP_ERROR, 0x8080F4);
    fn_SendMsg(sci, SCI_STYLESETITALIC, STYLE_LSP_ERROR, 1);

    fn_SendMsg(sci, SCI_STYLESETBACK, STYLE_LSP_WARN,  0x182C38);
    fn_SendMsg(sci, SCI_STYLESETFORE, STYLE_LSP_WARN,  0x60C8F0);
    fn_SendMsg(sci, SCI_STYLESETITALIC, STYLE_LSP_WARN, 1);
}

static void apply_diagnostics(void *sci) {
    if (!sci || !fn_SendMsg) return;
    long long doc_len = fn_SendMsg(sci, SCI_GETLENGTH, 0, 0);
    if (doc_len <= 0) return;

    fn_SendMsg(sci, SCI_SETINDICATORCURRENT, INDIC_LSP_ERROR, 0);
    fn_SendMsg(sci, SCI_INDICATORCLEARRANGE, 0, doc_len);
    fn_SendMsg(sci, SCI_SETINDICATORCURRENT, INDIC_LSP_WARN, 0);
    fn_SendMsg(sci, SCI_INDICATORCLEARRANGE, 0, doc_len);
    fn_SendMsg(sci, SCI_ANNOTATIONCLEARALL, 0, 0);

    EnterCriticalSection(&g_cs);
    for (int i = 0; i < g_diag_count; i++) {
        int line = g_diags[i].line;
        long long start = fn_SendMsg(sci, SCI_POSITIONFROMLINE, line, 0);
        long long end   = fn_SendMsg(sci, SCI_GETLINEENDPOSITION, line, 0);
        if (start < 0 || end <= start) continue;

        char *buf = (char*)malloc(doc_len + 2);
        if (buf) {
            fn_SendMsg(sci, SCI_GETTEXT, doc_len + 1, (long long)buf);
            while (start < end && (buf[start] == ' ' || buf[start] == '\t')) start++;
            free(buf);
        }

        int indic = g_diags[i].is_err ? INDIC_LSP_ERROR : INDIC_LSP_WARN;
        fn_SendMsg(sci, SCI_SETINDICATORCURRENT, indic, 0);
        fn_SendMsg(sci, SCI_INDICATORFILLRANGE, start, end - start);

        if (g_diags[i].is_err || g_show_annotations) {
            fn_SendMsg(sci, SCI_ANNOTATIONSETTEXT, line, (long long)g_diags[i].msg);
            fn_SendMsg(sci, SCI_ANNOTATIONSETSTYLE, line, g_diags[i].is_err ? STYLE_LSP_ERROR : STYLE_LSP_WARN);
        }
    }
    fn_SendMsg(sci, SCI_ANNOTATIONSETVISIBLE, ANNOTATION_BOXED, 0);
    LeaveCriticalSection(&g_cs);
}

static DWORD WINAPI run_compiler_thread(LPVOID arg) {
    char *code = (char*)arg;
    char temp_dir[MAX_PATH];
    if (!GetTempPathA(sizeof(temp_dir), temp_dir)) {
        free(code);
        InterlockedExchange(&g_checker_busy, 0);
        return 0;
    }

    char tmp_src[MAX_PATH];
    snprintf(tmp_src, sizeof(tmp_src), "%scb_lsp_live.cpp", temp_dir);
    FILE *fp = fopen(tmp_src, "w");
    if (!fp) {
        free(code);
        InterlockedExchange(&g_checker_busy, 0);
        return 0;
    }
    fputs(code, fp);
    fclose(fp);
    free(code);

    const char *gpp_exe = "g++.exe";
    if (GetFileAttributesA("C:\\msys64\\ucrt64\\bin\\g++.exe") != INVALID_FILE_ATTRIBUTES) {
        gpp_exe = "C:\\msys64\\ucrt64\\bin\\g++.exe";
    }

    char cmd[MAX_PATH * 2];
    snprintf(cmd, sizeof(cmd), "\"%s\" -fsyntax-only -Wall -Wextra -Wformat=2 -fdiagnostics-color=never \"%s\"", gpp_exe, tmp_src);

    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    HANDLE hRead = NULL, hWrite = NULL;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) {
        InterlockedExchange(&g_checker_busy, 0);
        return 0;
    }
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;
    ZeroMemory(&pi, sizeof(pi));

    if (!CreateProcessA(NULL, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(hRead);
        CloseHandle(hWrite);
        InterlockedExchange(&g_checker_busy, 0);
        return 0;
    }
    CloseHandle(hWrite);

    char out_buf[16384];
    DWORD total_read = 0, bytes_read = 0;
    while (total_read < sizeof(out_buf) - 1 &&
           ReadFile(hRead, out_buf + total_read, (DWORD)(sizeof(out_buf) - 1 - total_read), &bytes_read, NULL) &&
           bytes_read > 0) {
        total_read += bytes_read;
    }
    out_buf[total_read] = '\0';

    CloseHandle(hRead);
    WaitForSingleObject(pi.hProcess, 3000);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    DiagItem local_diags[64];
    int count = 0;
    char *saveptr = NULL;
    char *line_str = strtok_s(out_buf, "\r\n", &saveptr);
    while (line_str && count < 64) {
        char *match = strstr(line_str, "cb_lsp_live.cpp:");
        if (match) {
            char *p = match + 16;
            int line_num = atoi(p);
            if (line_num > 0) {
                p = strchr(p, ':');
                if (p) {
                    p++;
                    if (*p >= '0' && *p <= '9') {
                        p = strchr(p, ':');
                        if (p) p++;
                    }
                    if (p) {
                        while (*p == ' ') p++;
                        int is_err = -1;
                        const char *msg_start = NULL;
                        if (strncmp(p, "error:", 6) == 0 || strncmp(p, "fatal error:", 12) == 0) {
                            is_err = 1;
                            msg_start = strchr(p, ':') + 1;
                        } else if (strncmp(p, "warning:", 8) == 0) {
                            is_err = 0;
                            msg_start = p + 8;
                        }
                        if (is_err >= 0 && msg_start) {
                            while (*msg_start == ' ') msg_start++;
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
                                         "  %s %s  ", is_err ? "[Error]" : "[Warning]", msg_start);
                                count++;
                            }
                        }
                    }
                }
            }
        }
        line_str = strtok_s(NULL, "\r\n", &saveptr);
    }

    EnterCriticalSection(&g_cs);
    memcpy(g_diags, local_diags, sizeof(DiagItem) * count);
    g_diag_count = count;
    LeaveCriticalSection(&g_cs);

    InterlockedExchange(&g_checker_busy, 0);
    return 0;
}

static void hover_tick(void) {
    void *sci = get_active_scintilla();
    if (!sci || !fn_SendMsg || !fn_wxGetMousePos || !fn_ScreenToClient) return;

    int mx = 0, my = 0;
    fn_wxGetMousePos(&mx, &my);
    fn_ScreenToClient(sci, &mx, &my);

    long long pos = fn_SendMsg(sci, SCI_POSITIONFROMPOINTCLOSE, (unsigned long long)mx, (long long)my);
    if (pos >= 0) {
        long long line = fn_SendMsg(sci, SCI_LINEFROMPOSITION, (unsigned long long)pos, 0);
        EnterCriticalSection(&g_cs);
        int found = -1;
        for (int i = 0; i < g_diag_count; i++) {
            if (g_diags[i].line == (int)line) {
                found = i;
                break;
            }
        }
        if (found >= 0) {
            if (!g_hover_calltip_active || g_last_hover_line != (int)line) {
                fn_SendMsg(sci, SCI_CALLTIPSETBACK, 0x262525, 0);
                fn_SendMsg(sci, SCI_CALLTIPSETFORE, g_diags[found].is_err ? 0x8080F4 : 0x60C8F0, 0);
                fn_SendMsg(sci, SCI_CALLTIPSHOW, (unsigned long long)pos, (long long)g_diags[found].msg);
                g_hover_calltip_active = 1;
                g_last_hover_line = (int)line;
            }
            LeaveCriticalSection(&g_cs);
            return;
        }
        LeaveCriticalSection(&g_cs);
    }

    if (g_hover_calltip_active) {
        fn_SendMsg(sci, SCI_CALLTIPCANCEL, 0, 0);
        g_hover_calltip_active = 0;
        g_last_hover_line = -1;
    }
}

static void periodic_tick(void) {
    g_tick_counter++;
    if (g_tick_counter >= 6) g_startup_ready = 1;
    if (!g_startup_ready) return;

    void *sci = get_active_scintilla();
    if (!sci || !fn_SendMsg) return;

    apply_scintilla_opacity(sci);

    char active_file[MAX_PATH];
    if (get_active_filepath(active_file, sizeof(active_file))) {
        DWORD attr = GetFileAttributesA(active_file);
        if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
            sync_folder_workspace(active_file, 1);
        } else {
            ensure_warmup_workspace_loaded();
        }
    }

    long long len = fn_SendMsg(sci, SCI_GETLENGTH, 0, 0);
    if (len <= 0 || len > 250000) return;

    char *buf = (char*)malloc((size_t)len + 2);
    if (!buf) return;
    fn_SendMsg(sci, SCI_GETTEXT, (unsigned long long)(len + 1), (long long)buf);
    uint64_t h = hash_str(buf, (size_t)len);

    if ((h != g_last_hash || sci != g_last_sci) && InterlockedCompareExchange(&g_checker_busy, 1, 0) == 0) {
        g_last_hash = h;
        g_last_sci = sci;
        HANDLE hThread = CreateThread(NULL, 0, run_compiler_thread, buf, 0, NULL);
        if (hThread) {
            CloseHandle(hThread);
        } else {
            free(buf);
            InterlockedExchange(&g_checker_busy, 0);
        }
    } else {
        free(buf);
        apply_diagnostics(sci);
    }
}

static void launch_debugger_win(int asan) {
    char appdata[MAX_PATH];
    if (!GetEnvironmentVariableA("APPDATA", appdata, sizeof(appdata))) return;
    char cmd[MAX_PATH * 2];
    snprintf(cmd, sizeof(cmd),
             "powershell.exe -ExecutionPolicy Bypass -File \"%s\\CodeBlocks\\bin\\cb_single_file_debug.ps1\"%s",
             appdata, asan ? " -asan" : "");
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));
    if (CreateProcessA(NULL, cmd, NULL, NULL, FALSE, CREATE_NEW_CONSOLE, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
}

static void launch_cph_judge_win(void) {
    char appdata[MAX_PATH];
    if (!GetEnvironmentVariableA("APPDATA", appdata, sizeof(appdata))) return;
    char cmd[MAX_PATH * 2];
    snprintf(cmd, sizeof(cmd),
             "pythonw.exe \"%s\\CodeBlocks\\bin\\cb_cph_judge.py\" --run",
             appdata);
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));
    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        snprintf(cmd, sizeof(cmd),
                 "python.exe \"%s\\CodeBlocks\\bin\\cb_cph_judge.py\" --run",
                 appdata);
        if (CreateProcessA(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
    } else {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
}

static LRESULT CALLBACK keyboard_hook_proc(int code, WPARAM wParam, LPARAM lParam) {
    if (code == HC_ACTION && !(lParam & (1UL << 31))) { // Key press
        int ctrl  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        int shift = (GetKeyState(VK_SHIFT)   & 0x8000) != 0;
        int alt   = (GetKeyState(VK_MENU)    & 0x8000) != 0;

        // F5 / Ctrl+F5 -> Single-File GDB Debugger / AddressSanitizer
        if (wParam == VK_F5 && !alt && !shift) {
            launch_debugger_win(ctrl);
            return 1;
        }

        // F6 or Ctrl+Alt+B -> CPH Judge
        if ((wParam == VK_F6 && !ctrl && !alt && !shift) ||
            (wParam == 'B' && ctrl && alt && !shift)) {
            launch_cph_judge_win();
            return 1;
        }

        // Ctrl+Shift+M -> Toggle inline warning Error Lens boxes
        if (wParam == 'M' && ctrl && shift && !alt) {
            g_show_annotations = !g_show_annotations;
            void *sci = get_active_scintilla();
            if (sci) apply_diagnostics(sci);
            return 1;
        }

        // Ctrl + I / J / K / L cursor navigation (and Shift selection / Alt word jump)
        if (ctrl && (wParam == 'I' || wParam == 'J' || wParam == 'K' || wParam == 'L')) {
            void *sci = get_active_scintilla();
            if (sci && fn_SendMsg) {
                unsigned int sci_cmd = 0;
                if (wParam == 'I' && !alt) {
                    sci_cmd = shift ? SCI_LINEUPEXTEND : SCI_LINEUP;
                } else if (wParam == 'K' && !alt) {
                    sci_cmd = shift ? SCI_LINEDOWNEXTEND : SCI_LINEDOWN;
                } else if (wParam == 'J') {
                    if (alt) sci_cmd = shift ? SCI_WORDLEFTEXTEND : SCI_WORDLEFT;
                    else     sci_cmd = shift ? SCI_CHARLEFTEXTEND : SCI_CHARLEFT;
                } else if (wParam == 'L') {
                    if (alt) sci_cmd = shift ? SCI_WORDRIGHTEXTEND : SCI_WORDRIGHT;
                    else     sci_cmd = shift ? SCI_CHARRIGHTEXTEND : SCI_CHARRIGHT;
                }
                if (sci_cmd != 0) {
                    fn_SendMsg(sci, sci_cmd, 0, 0);
                    return 1;
                }
            }
        }
    }
    return CallNextHookEx(g_kbd_hook, code, wParam, lParam);
}

static VOID CALLBACK main_timer_proc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime) {
    (void)hwnd; (void)uMsg; (void)idEvent; (void)dwTime;
    static int sub_tick = 0;
    if (fn_IsAppShuttingDown && fn_IsAppShuttingDown()) return;
    hover_tick();
    sub_tick++;
    if (sub_tick >= 4) {
        sub_tick = 0;
        periodic_tick();
    }
}

static LRESULT CALLBACK getmsg_hook_proc(int code, WPARAM wParam, LPARAM lParam) {
    if (code >= 0 && InterlockedCompareExchange(&g_timer_installed, 1, 0) == 0) {
        SetTimer(NULL, 0, 150, main_timer_proc);
    }
    return CallNextHookEx(g_getmsg_hook, code, wParam, lParam);
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    (void)lpvReserved;
    switch (fdwReason) {
        case DLL_PROCESS_ATTACH: {
            DisableThreadLibraryCalls(hinstDLL);
            // Pin DLL in memory so Code::Blocks PluginManager::ScanForPlugins doesn't unload it
            HMODULE hSelf = NULL;
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                               (LPCWSTR)DllMain, &hSelf);
            InitializeCriticalSection(&g_cs);
            g_main_thread_id = GetCurrentThreadId();
            g_getmsg_hook = SetWindowsHookExW(WH_GETMESSAGE, getmsg_hook_proc, NULL, g_main_thread_id);
            g_kbd_hook    = SetWindowsHookExW(WH_KEYBOARD,   keyboard_hook_proc, NULL, g_main_thread_id);
            dbg_log("DllMain: attached on thread %lu (hooks: %p, %p)", (unsigned long)g_main_thread_id, (void*)g_getmsg_hook, (void*)g_kbd_hook);
            break;
        }
        case DLL_PROCESS_DETACH:
            if (g_getmsg_hook) UnhookWindowsHookEx(g_getmsg_hook);
            if (g_kbd_hook)    UnhookWindowsHookEx(g_kbd_hook);
            DeleteCriticalSection(&g_cs);
            break;
    }
    return TRUE;
}
