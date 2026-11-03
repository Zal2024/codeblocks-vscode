#define _GNU_SOURCE
#include <stdarg.h>
#include <ctype.h>
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
typedef void* (*Manager_GetAppWindow_fn)(void *mgr);
typedef int   (*Manager_IsAppShuttingDown_fn)(void);
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
typedef void* (*wxFindWindowAtPointer_fn)(int *pt);
typedef void* (*wxWindowBase_FindFocus_fn)(void);
typedef void* (*ProjectManager_FindProjectForFile_fn)(void *pmgr, const void *wxstr_path, void **result, int is_relative, int is_unix);
typedef int   (*ProjectManager_IsProjectStillOpen_fn)(void *pmgr, void *proj);

static Manager_Get_fn fn_Manager_Get = NULL;
static Manager_GetAppWindow_fn fn_GetAppWindow = NULL;
static Manager_IsAppShuttingDown_fn fn_IsAppShuttingDown = NULL;
static Manager_GetEditorManager_fn fn_GetEditorManager = NULL;
static int g_startup_ready = 0;
static int g_tick_counter = 0;
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
static wxFindWindowAtPointer_fn fn_FindWindowAtPointer = NULL;
static wxWindowBase_FindFocus_fn fn_FindFocus = NULL;
static ProjectManager_FindProjectForFile_fn fn_FindProjectForFile = NULL;
static ProjectManager_IsProjectStillOpen_fn fn_IsProjectStillOpen = NULL;

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
#define SCI_GETCURRENTPOS           2008
#define SCI_GETANCHOR               2009
#define SCI_GETSTYLEDTEXT           2015
#define SCI_GETEOLMODE              2030
#define SCI_INSERTTEXT              2003
#define SCI_GETCHARAT               2007
#define SCI_BEGINUNDOACTION         2078
#define SCI_ENDUNDOACTION           2079
#define SCI_GETCARETLINEBACK        2097
#define SCI_SETLINEINDENTATION      2126
#define SCI_GETLINEINDENTATION      2127
#define SCI_GETLINEINDENTPOSITION   2128
#define SCI_GETSELECTIONSTART       2143
#define SCI_GETSELECTIONEND         2145
#define SCI_GETLINECOUNT            2154
#define SCI_SETSEL                  2160
#define SCI_GETTEXTRANGE            2162
#define SCI_GETSEARCHFLAGS          2199
#define SCI_SETSEARCHFLAGS          2198
#define SCI_GOTOPOS                 2025
#define SCI_SETENDATLASTLINE        2277
#define SCI_TEXTHEIGHT              2279
#define SCI_LINEUP                  2302
#define SCI_HOME                    2312
#define SCI_LINEEND                 2314
#define SCI_NEWLINE                 2329
#define SCI_GETZOOM                 2374
#define SCI_STYLEGETSIZE            2485
#define SCI_GETEXTRAASCENT          2526
#define SCI_SETADDITIONALSELECTIONTYPING 2565
#define SCI_SETMULTIPASTE           2614
#define SCI_DELETERANGE             2645
#define SCI_MULTIPLESELECTADDNEXT   2688
#define SCI_TARGETWHOLEDOCUMENT     2690
#define SCI_COLOURISE               4003

#define INDIC_TEXTFORE              17
#define SCFIND_MATCHCASE            4

// wxScintilla lexer style ids for the C/C++ lexer (SCE_C_*)
#define SCE_C_WORD                  5
#define SCE_C_STRING                6
#define SCE_C_CHARACTER             7
#define SCE_C_PREPROCESSOR          9
#define SCE_C_OPERATOR              10
#define SCE_C_IDENTIFIER            11

// Dark+ token overlay, drawn as text-colour indicators (values from VS Code's
// dark_plus.json / editorColorRegistry.ts). Kept above the LSP range and C::B's own.
#define INDIC_FUNCTION              20
#define INDIC_CONTROL               21
#define INDIC_INCLUDE               22
#define INDIC_ESCAPE                23
#define INDIC_BRACKET1              24
#define INDIC_BRACKET2              25
#define INDIC_BRACKET3              26
#define INDIC_BRACKET_BAD           27
#define INDIC_PLAIN                 28

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

typedef struct {
    char *code;
    void *sci;
    int lang;
    char dir[1024];
} CheckJob;

enum { LANG_NONE = 0, LANG_C = 1, LANG_CPP = 2 };

static DiagItem g_diags[64];
static int g_diag_count = 0;
static void *g_diag_sci = NULL;      // Editor g_diags belongs to; compared, never dereferenced
static unsigned g_diag_gen = 0;      // Bumped whenever g_diags changes
static unsigned g_applied_gen = 0;
static void *g_applied_sci = NULL;
static int g_applied_annotations = -1;
static uint64_t g_last_hash = 0;
static void *g_last_sci = NULL;
static int g_show_annotations = 0; // Default: yellow warnings only show squiggle + hover tooltip
static int g_checker_busy = 0;
static int g_hover_calltip_active = 0;
static int g_last_hover_line = -1;
static int g_hover_candidate_line = -1;
static int g_hover_dwell = 0;
static char g_lsp_dir[64] = "";      // Private per-process dir, so two windows never share a buffer
static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;

// Editor styling state
static void *g_styled_sci = NULL;
static int g_styled_zoom = 0;
static void *g_overlay_sci = NULL;
static long g_overlay_len = -1;
static uint64_t g_overlay_hash = 0;

// Auto-workspace tracking
static char g_workspace_dir[1024] = "";
static char g_seen_files[256][256];
static int g_seen_count = 0;
static char g_last_active[1024] = "";
static void *g_our_proj = NULL;      // The auto-generated project; compared, never dereferenced
static void *g_file_owner = NULL;    // Project containing the active file


static void resolve_cb_symbols(void) {
    if (fn_Manager_Get) return;
    fn_Manager_Get = (Manager_Get_fn)dlsym(RTLD_DEFAULT, "_ZN7Manager3GetEv");
    fn_GetAppWindow = (Manager_GetAppWindow_fn)dlsym(RTLD_DEFAULT, "_ZNK7Manager12GetAppWindowEv");
    fn_IsAppShuttingDown = (Manager_IsAppShuttingDown_fn)dlsym(RTLD_DEFAULT, "_ZN7Manager17IsAppShuttingDownEv");
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
    fn_FindWindowAtPointer = (wxFindWindowAtPointer_fn)dlsym(RTLD_DEFAULT, "_Z21wxFindWindowAtPointerR7wxPoint");
    fn_FindFocus = (wxWindowBase_FindFocus_fn)dlsym(RTLD_DEFAULT, "_ZN12wxWindowBase9FindFocusEv");
    fn_FindProjectForFile = (ProjectManager_FindProjectForFile_fn)dlsym(RTLD_DEFAULT, "_ZN14ProjectManager18FindProjectForFileERK8wxStringPP11ProjectFilebb");
    fn_IsProjectStillOpen = (ProjectManager_IsProjectStillOpen_fn)dlsym(RTLD_DEFAULT, "_ZN14ProjectManager18IsProjectStillOpenEP9cbProject");
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

static const char *file_ext(const char *path) {
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    const char *dot = strrchr(base, '.');
    return dot ? dot : "";
}

// Same split as VS Code's c/cpp language contributions: .h is C++.
static int lang_for_path(const char *path) {
    const char *e = file_ext(path);
    if (!strcmp(e, ".c")) return LANG_C;
    if (!strcmp(e, ".cpp") || !strcmp(e, ".cc") || !strcmp(e, ".cxx") ||
        !strcmp(e, ".h") || !strcmp(e, ".hpp") || !strcmp(e, ".hh") || !strcmp(e, ".hxx")) return LANG_CPP;
    return LANG_NONE;
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

static void remember_file(const char *name) {
    if (g_seen_count >= 256) return;
    snprintf(g_seen_files[g_seen_count], sizeof(g_seen_files[0]), "%s", name);
    g_seen_count++;
}

static void fprint_xml_attr(FILE *fp, const char *s) {
    for (; *s; s++) {
        switch (*s) {
            case '&':  fputs("&amp;", fp); break;
            case '<':  fputs("&lt;", fp); break;
            case '>':  fputs("&gt;", fp); break;
            case '"':  fputs("&quot;", fp); break;
            default:   fputc(*s, fp); break;
        }
    }
}

static void* find_file_owner(void *pmgr, const char *file) {
    if (!fn_FindProjectForFile || !fn_cbC2U) return NULL;
    unsigned char wxstr[64];
    memset(wxstr, 0, sizeof(wxstr));
    fn_cbC2U(wxstr, file);
    return fn_FindProjectForFile(pmgr, wxstr, NULL, 0, 0);
}

// Keep g_our_proj in ProjectManager::m_pProjects so the left Projects -> Workspace
// tree shows all files in the folder, but clear m_pActiveProject (offset 0x68) and
// cbEditor::m_pProjectFile (offset 0x448) so CompilerGCC (F9 / Ctrl+F9 / Ctrl+F10 /
// toolbar Build & Run) always compiles and runs the active editor file standalone!
static void detach_our_project_from_compiler(void) {
    if (!g_our_proj || !fn_Manager_Get || !fn_GetProjectManager) return;
    void *mgr = fn_Manager_Get();
    if (!mgr) return;
    void *pmgr = fn_GetProjectManager(mgr);
    if (pmgr && *(void**)((char*)pmgr + 0x68) == g_our_proj) {
        *(void**)((char*)pmgr + 0x68) = NULL;
    }
    void *eb = get_active_editor_base();
    if (eb && fn_GetEditorManager && fn_GetBuiltinEditor) {
        void *emgr = fn_GetEditorManager(mgr);
        void *cbed = emgr ? fn_GetBuiltinEditor(emgr, eb) : NULL;
        if (cbed) {
            void *pf = *(void**)((char*)cbed + 0x448);
            if (pf && *(void**)((char*)pf + 0x230) == g_our_proj) {
                *(void**)((char*)cbed + 0x448) = NULL;
            }
        }
    }
}

static void clear_project_modified(void *pmgr, void *proj) {
    if (proj && fn_ProjSetModified) fn_ProjSetModified(proj, 0);
    if (fn_GetWorkspace && fn_WsSetModified) {
        void *ws = fn_GetWorkspace(pmgr);
        if (ws) fn_WsSetModified(ws, 0);
    }
    void *ui = *(void**)((char*)pmgr + 0x60);
    if (ui) {
        void (**vtable)(void*) = *(void (***)(void*))ui;
        if (vtable && vtable[4]) vtable[4](ui); // m_ui->RebuildTree() at offset 0x20
    }
    detach_our_project_from_compiler();
}

static void sync_folder_workspace(const char *active_file) {
    if (!active_file || active_file[0] != '/') return;

    // Only on change, so two windows do not overwrite each other every tick
    int file_changed = strcmp(g_last_active, active_file) != 0;
    if (file_changed) {
        snprintf(g_last_active, sizeof(g_last_active), "%s", active_file);
        FILE *af = fopen("/tmp/.cb_active_file", "w");
        if (af) {
            fprintf(af, "%s\n", active_file);
            fclose(af);
        }
    }

    if (!fn_GetProjectManager || !fn_cbC2U || !fn_LoadProject) return;
    void *mgr = fn_Manager_Get();
    if (!mgr) return;
    void *pmgr = fn_GetProjectManager(mgr);
    if (!pmgr) return;

    if (g_our_proj && fn_IsProjectStillOpen && !fn_IsProjectStillOpen(pmgr, g_our_proj)) {
        g_our_proj = NULL;
        g_workspace_dir[0] = '\0';
    }

    char dir[1024];
    strncpy(dir, active_file, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = '\0';
    char *slash = strrchr(dir, '/');
    if (!slash || slash == dir) return;
    *slash = '\0';

    const char *folder_name = strrchr(dir, '/');
    folder_name = folder_name ? (folder_name + 1) : dir;

    void *active_proj = *(void**)((char*)pmgr + 0x68);

    // Never displace a project the user opened: leave its files, and any file
    // opened while it is active, to Code::Blocks.
    if (file_changed || !g_file_owner) g_file_owner = find_file_owner(pmgr, active_file);
    if (g_file_owner && g_file_owner != g_our_proj) return;
    if (active_proj && active_proj != g_our_proj) return;

    // Case 1: New folder or no folder project loaded yet -> build .vscode_workspace.cbp & load it
    if (strcmp(g_workspace_dir, dir) != 0 || !g_our_proj) {
        DIR *dp = opendir(dir);
        if (!dp) return;

        char cbp_path[1100];
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
        fprintf(fp, "\t\t<Option title=\"");
        fprint_xml_attr(fp, folder_name);
        fprintf(fp, "\" />\n");
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
        while ((de = readdir(dp)) != NULL && g_seen_count < 256) {
            if (is_source_or_project_file(de->d_name)) {
                fprintf(fp, "\t\t<Unit filename=\"");
                fprint_xml_attr(fp, de->d_name);
                fprintf(fp, "\">\n\t\t\t<Option compile=\"0\" />\n\t\t\t<Option link=\"0\" />\n\t\t</Unit>\n");
                remember_file(de->d_name);
            }
        }
        closedir(dp);

        fprintf(fp, "\t</Project>\n");
        fprintf(fp, "</CodeBlocks_project_file>\n");
        fclose(fp);

        snprintf(g_workspace_dir, sizeof(g_workspace_dir), "%s", dir);

        unsigned char wxstr[64];
        memset(wxstr, 0, sizeof(wxstr));
        fn_cbC2U(wxstr, cbp_path);
        void *proj = fn_LoadProject(pmgr, wxstr, 1);
        g_our_proj = proj;
        g_file_owner = proj;
        clear_project_modified(pmgr, proj);
        return;
    }

    // Case 2: Same folder, check if user saved a brand-new file into this folder
    if (!g_our_proj || !fn_AddFileToProject) return;
    DIR *dp = opendir(dir);
    if (!dp) return;
    int added_any = 0;
    struct dirent *de;
    while ((de = readdir(dp)) != NULL && g_seen_count < 256) {
        if (is_source_or_project_file(de->d_name) && !was_file_seen(de->d_name)) {
            remember_file(de->d_name);
            char full_p[1400];
            snprintf(full_p, sizeof(full_p), "%s/%s", dir, de->d_name);
            unsigned char wxstr[64];
            memset(wxstr, 0, sizeof(wxstr));
            fn_cbC2U(wxstr, full_p);
            fn_AddFileToProject(pmgr, wxstr, g_our_proj, 0);
            added_any = 1;
        }
    }
    closedir(dp);

    if (added_any) clear_project_modified(pmgr, g_our_proj);
    else detach_our_project_from_compiler();
}

static uint64_t hash_str(const char *s, size_t len) {
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < len; i++) {
        h ^= (unsigned char)s[i];
        h *= 1099511628211ULL;
    }
    return h;
}

static int editor_style_is_stale(void *sci) {
    return sci != g_styled_sci ||
           fn_SendMsg(sci, SCI_STYLEGETBACK, 32, 0) != 0x1E1E1E ||
           fn_SendMsg(sci, SCI_GETCARETLINEBACK, 0, 0) != 0x282828 ||
           fn_SendMsg(sci, SCI_GETZOOM, 0, 0) != g_styled_zoom;
}

// VS Code: lineHeight = round(1.35 * fontSize) on Linux and Windows (fontInfo.ts, GOLDEN_LINE_HEIGHT_RATIO).
// Whatever the font does not already provide is split above and below the line.
static void apply_line_height(void *sci) {
    long zoom = fn_SendMsg(sci, SCI_GETZOOM, 0, 0);
    long points = fn_SendMsg(sci, SCI_STYLEGETSIZE, 32, 0) + zoom;
    if (points <= 0) return;
    long extra_now = fn_SendMsg(sci, SCI_GETEXTRAASCENT, 0, 0) * 2;
    long natural = fn_SendMsg(sci, SCI_TEXTHEIGHT, 0, 0) - extra_now;
    long target = (long)(points * 96.0 / 72.0 * 1.35 + 0.5);
    long extra = target - natural;
    if (extra < 0) extra = 0;
    fn_SendMsg(sci, SCI_SETEXTRAASCENT, extra / 2, 0);
    fn_SendMsg(sci, SCI_SETEXTRADESCENT, extra - extra / 2, 0);
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
    fn_SendMsg(sci, SCI_SETCARETFORE, 0xADAFAE, 0);     // Caret color #AEAFAD
    fn_SendMsg(sci, SCI_SETFOLDMARGINCOLOUR, 1, 0x1E1E1E);
    fn_SendMsg(sci, SCI_SETFOLDMARGINHICOLOUR, 1, 0x1E1E1E);

    // 1. 50% Opacity Highlights (Selection & Matching Braces)
    fn_SendMsg(sci, SCI_STYLESETBACK, 34, 0x58462A); // #2A4658 (50% steel-blue)
    fn_SendMsg(sci, SCI_STYLESETFORE, 34, 0xDCDCDC); // #DCDCDC
    fn_SendMsg(sci, SCI_STYLESETBOLD, 34, 0);
    fn_SendMsg(sci, SCI_SETSELALPHA, 128, 0);        // 50% selection transparency

    // 2. VS Code typography: LCD subpixel AA + buffered draw (line height is set below, once fonts are final)
    fn_SendMsg(sci, SCI_SETFONTQUALITY, 3, 0);
    fn_SendMsg(sci, SCI_SETBUFFEREDDRAW, 1, 0);
    fn_SendMsg(sci, SCI_INDICSETALPHA, 8, 50);
    fn_SendMsg(sci, SCI_INDICSETOUTLINEALPHA, 8, 85);

    // 3. Configure VS Code LSP Squiggle Indicators
    fn_SendMsg(sci, SCI_INDICSETSTYLE, INDIC_LSP_ERROR, INDIC_SQUIGGLE);
    fn_SendMsg(sci, SCI_INDICSETFORE,  INDIC_LSP_ERROR, 0x4C4CF1); // #F14C4C editorError.foreground
    fn_SendMsg(sci, SCI_INDICSETSTYLE, INDIC_LSP_WARN,  INDIC_SQUIGGLE);
    fn_SendMsg(sci, SCI_INDICSETFORE,  INDIC_LSP_WARN,  0x00A7CC); // #CCA700 editorWarning.foreground

    // 4. Configure VS Code Error Lens Annotation Styles
    fn_SendMsg(sci, SCI_STYLESETBACK, STYLE_LSP_ERROR, 0x201D3A);  // #3A1D20 muted dark red box
    fn_SendMsg(sci, SCI_STYLESETFORE, STYLE_LSP_ERROR, 0x8080F4);  // #F48080 soft red text
    fn_SendMsg(sci, SCI_STYLESETITALIC, STYLE_LSP_ERROR, 1);

    fn_SendMsg(sci, SCI_STYLESETBACK, STYLE_LSP_WARN,  0x182C38);  // #382C18 muted dark amber box
    fn_SendMsg(sci, SCI_STYLESETFORE, STYLE_LSP_WARN,  0x60C8F0);  // #F0C860 soft amber text
    fn_SendMsg(sci, SCI_STYLESETITALIC, STYLE_LSP_WARN, 1);

    // 5. Dark+ token overlay and bracket pair colourisation
    static const struct { int indic; int bgr; } overlay[] = {
        { INDIC_FUNCTION,    0xAADCDC }, // #DCDCAA entity.name.function
        { INDIC_CONTROL,     0xC086C5 }, // #C586C0 keyword.control
        { INDIC_INCLUDE,     0x7891CE }, // #CE9178 meta.preprocessor.string
        { INDIC_ESCAPE,      0x7DBAD7 }, // #D7BA7D constant.character.escape
        { INDIC_BRACKET1,    0x00D7FF }, // #FFD700 editorBracketHighlight.foreground1
        { INDIC_BRACKET2,    0xD670DA }, // #DA70D6 editorBracketHighlight.foreground2
        { INDIC_BRACKET3,    0xFF9F17 }, // #179FFF editorBracketHighlight.foreground3
        { INDIC_BRACKET_BAD, 0x1818F4 }, // #FF1212 at 80% over #1E1E1E, unexpectedBracket.foreground
        { INDIC_PLAIN,       0xD4D4D4 }, // #D4D4D4 editor.foreground, C++ words used as C identifiers
    };
    for (size_t i = 0; i < sizeof(overlay) / sizeof(overlay[0]); i++) {
        fn_SendMsg(sci, SCI_INDICSETSTYLE, overlay[i].indic, INDIC_TEXTFORE);
        fn_SendMsg(sci, SCI_INDICSETFORE,  overlay[i].indic, overlay[i].bgr);
    }

    // 6. Editor behaviour defaults from editorOptions.ts
    fn_SendMsg(sci, SCI_SETENDATLASTLINE, 0, 0);               // scrollBeyondLastLine: true
    fn_SendMsg(sci, SCI_SETADDITIONALSELECTIONTYPING, 1, 0);   // Type at every cursor
    fn_SendMsg(sci, SCI_SETMULTIPASTE, 1, 0);                  // Paste at every cursor

    apply_line_height(sci);
    g_styled_sci = sci;
    g_styled_zoom = (int)fn_SendMsg(sci, SCI_GETZOOM, 0, 0);
}

typedef struct {
    long min;
    long max;
    char *text;
} SciTextRange;

static int word_in_list(const char *const *list, const char *word, size_t len) {
    for (int i = 0; list[i]; i++) {
        if (strlen(list[i]) == len && memcmp(list[i], word, len) == 0) return 1;
    }
    return 0;
}

// The lexer's keyword list is C++'s for both languages, but new, delete and friends
// are ordinary identifiers in C.
static int is_control_keyword(const char *word, size_t len, int lang) {
    static const char *const shared[] = {
        "if", "else", "for", "while", "do", "switch", "case", "default", "break",
        "continue", "return", "goto", NULL
    };
    static const char *const cpp_only[] = { "try", "catch", "throw", "new", "delete", "using", NULL };
    return word_in_list(shared, word, len) || (lang != LANG_C && word_in_list(cpp_only, word, len));
}

static int is_cpp_only_word(const char *word, size_t len) {
    static const char *const words[] = {
        "new", "delete", "class", "this", "template", "typename", "namespace", "public", "private",
        "protected", "virtual", "operator", "try", "catch", "throw", "using", "friend", "explicit", NULL
    };
    return word_in_list(words, word, len);
}

static void overlay_fill(void *sci, int indic, long start, long len) {
    fn_SendMsg(sci, SCI_SETINDICATORCURRENT, indic, 0);
    fn_SendMsg(sci, SCI_INDICATORFILLRANGE, start, len);
}

// Colours what Scintilla's lexer cannot tell apart, the way Dark+ does for C and C++.
static void apply_token_overlay(void *sci) {
    long len = fn_SendMsg(sci, SCI_GETLENGTH, 0, 0);
    if (len <= 0 || len > 250000) return;
    int lang = lang_for_path(g_last_active);

    fn_SendMsg(sci, SCI_COLOURISE, 0, -1);
    char *cells = (char*)malloc((size_t)len * 2 + 2);
    if (!cells) return;
    SciTextRange range = { 0, len, cells };
    fn_SendMsg(sci, SCI_GETSTYLEDTEXT, 0, (long)&range);
#define CH(i) (cells[(i) * 2])
#define ST(i) ((unsigned char)cells[(i) * 2 + 1])

    for (int indic = INDIC_FUNCTION; indic <= INDIC_PLAIN; indic++) {
        fn_SendMsg(sci, SCI_SETINDICATORCURRENT, indic, 0);
        fn_SendMsg(sci, SCI_INDICATORCLEARRANGE, 0, len);
    }

    static char stack[512];
    int depth = 0;
    for (long i = 0; i < len; i++) {
        int style = ST(i);
        char c = CH(i);

        if (style == SCE_C_OPERATOR && strchr("([{", c) && c) {
            overlay_fill(sci, INDIC_BRACKET1 + depth % 3, i, 1);
            if (depth < (int)sizeof(stack)) stack[depth++] = c;
        } else if (style == SCE_C_OPERATOR && strchr(")]}", c) && c) {
            char open = (c == ')') ? '(' : (c == ']') ? '[' : '{';
            if (depth > 0 && stack[depth - 1] == open) {
                depth--;
                overlay_fill(sci, INDIC_BRACKET1 + depth % 3, i, 1);
            } else {
                overlay_fill(sci, INDIC_BRACKET_BAD, i, 1);
            }
        } else if ((style == SCE_C_IDENTIFIER || style == SCE_C_WORD) && (i == 0 || ST(i - 1) != style)) {
            long end = i;
            char word[32];
            while (end < len && ST(end) == style) {
                if (end - i < (long)sizeof(word)) word[end - i] = CH(end);
                end++;
            }
            if (style == SCE_C_WORD) {
                if (end - i < (long)sizeof(word)) {
                    if (is_control_keyword(word, (size_t)(end - i), lang))
                        overlay_fill(sci, INDIC_CONTROL, i, end - i);
                    else if (lang == LANG_C && is_cpp_only_word(word, (size_t)(end - i)))
                        overlay_fill(sci, INDIC_PLAIN, i, end - i);
                }
            } else {
                long next = end;
                while (next < len && (CH(next) == ' ' || CH(next) == '\t')) next++;
                if (next < len && CH(next) == '(') overlay_fill(sci, INDIC_FUNCTION, i, end - i);
            }
            i = end - 1;
        } else if ((style == SCE_C_STRING || style == SCE_C_CHARACTER) && c == '\\' && i + 1 < len) {
            overlay_fill(sci, INDIC_ESCAPE, i, 2);
            i++;
        } else if (style == SCE_C_PREPROCESSOR && c == '#') {
            long p = i + 1;
            while (p < len && (CH(p) == ' ' || CH(p) == '\t')) p++;
            if (p + 7 <= len) {
                char directive[8];
                for (int k = 0; k < 7; k++) directive[k] = CH(p + k);
                directive[7] = '\0';
                if (strcmp(directive, "include") == 0) {
                    long q = p + 7;
                    while (q < len && (CH(q) == ' ' || CH(q) == '\t')) q++;
                    if (q < len && (CH(q) == '<' || CH(q) == '"')) {
                        char close = (CH(q) == '<') ? '>' : '"';
                        long r = q + 1;
                        while (r < len && CH(r) != close && CH(r) != '\n') r++;
                        if (r < len && CH(r) == close) overlay_fill(sci, INDIC_INCLUDE, q, r - q + 1);
                    }
                }
            }
        }
    }
#undef CH
#undef ST
    free(cells);
}

static void apply_diagnostics(void *sci) {
    if (!sci || !fn_SendMsg) return;

    pthread_mutex_lock(&g_mutex);
    int up_to_date = (g_applied_sci == sci && g_applied_gen == g_diag_gen &&
                      g_applied_annotations == g_show_annotations);
    int current = (g_diag_sci == sci);
    pthread_mutex_unlock(&g_mutex);
    if (up_to_date || !current) return;

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
        long start = fn_SendMsg(sci, SCI_GETLINEINDENTPOSITION, line, 0);
        long end   = fn_SendMsg(sci, SCI_GETLINEENDPOSITION, line, 0);
        if (start < 0 || end <= start) continue;

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
    g_applied_sci = sci;
    g_applied_gen = g_diag_gen;
    g_applied_annotations = g_show_annotations;
    pthread_mutex_unlock(&g_mutex);
}

static void shell_quote(char *out, size_t max, const char *s) {
    size_t n = 0;
    if (max < 8) return;
    out[n++] = '\'';
    for (; *s && n + 6 < max; s++) {
        if (*s == '\'') {
            memcpy(out + n, "'\\''", 4);
            n += 4;
        } else {
            out[n++] = *s;
        }
    }
    out[n++] = '\'';
    out[n] = '\0';
}

static void format_diag(DiagItem *item, int line, int is_err, const char *text) {
    item->line = line;
    item->is_err = is_err;
    snprintf(item->msg, sizeof(item->msg), "  %s %s  ",
             is_err ? "\xE2\x9B\x94 [Error]" : "\xE2\x9A\xA0 [Warning]", text);
}

static void* run_compiler_thread(void *arg) {
    CheckJob *job = (CheckJob*)arg;
    DiagItem *local_diags = (DiagItem*)malloc(sizeof(g_diags));
    int count = 0;

    // Check C as C and C++ as C++, with the file's own folder on the include path
    char tmp_src[128];
    snprintf(tmp_src, sizeof(tmp_src), "%s/live.%s", g_lsp_dir, job->lang == LANG_C ? "c" : "cpp");
    FILE *fp = local_diags ? fopen(tmp_src, "w") : NULL;
    if (fp) {
        fputs(job->code, fp);
        fclose(fp);

        char quoted_dir[2200], cmd[2600];
        shell_quote(quoted_dir, sizeof(quoted_dir), job->dir);
        snprintf(cmd, sizeof(cmd),
                 "LC_ALL=C %s -fsyntax-only -Wall -Wextra -Wformat=2 -fdiagnostics-color=never -I%s '%s' 2>&1",
                 job->lang == LANG_C ? "gcc" : "g++", quoted_dir, tmp_src);

        char prefix[140];
        int prefix_len = snprintf(prefix, sizeof(prefix), "%s:", tmp_src);
        FILE *pipe = popen(cmd, "r");
        if (pipe) {
            char line_buf[512];
            while (fgets(line_buf, sizeof(line_buf), pipe) && count < 64) {
                if (strncmp(line_buf, prefix, (size_t)prefix_len) != 0) continue;
                char *p = line_buf + prefix_len;
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

                // One entry per line. An error takes over from a warning, text included.
                int merged = 0;
                for (int j = 0; j < count; j++) {
                    if (local_diags[j].line == line_num - 1) {
                        if (is_err && !local_diags[j].is_err)
                            format_diag(&local_diags[j], line_num - 1, 1, msg_start);
                        merged = 1;
                        break;
                    }
                }
                if (!merged) format_diag(&local_diags[count++], line_num - 1, is_err, msg_start);
            }
            pclose(pipe);

            pthread_mutex_lock(&g_mutex);
            memcpy(g_diags, local_diags, sizeof(DiagItem) * (size_t)count);
            g_diag_count = count;
            g_diag_sci = job->sci;
            g_diag_gen++;
            pthread_mutex_unlock(&g_mutex);
        }
    }

    free(local_diags);
    free(job->code);
    free(job);
    __atomic_store_n(&g_checker_busy, 0, __ATOMIC_RELEASE);
    return NULL;
}

static void hide_hover(void *sci) {
    if (g_hover_calltip_active) {
        fn_SendMsg(sci, SCI_CALLTIPCANCEL, 0, 0);
        g_hover_calltip_active = 0;
    }
    g_last_hover_line = -1;
    g_hover_candidate_line = -1;
    g_hover_dwell = 0;
}

// Refreshes colours soon after a keystroke rather than on the slower periodic tick
static void refresh_overlay_if_edited(void *sci) {
    long len = fn_SendMsg(sci, SCI_GETLENGTH, 0, 0);
    if (sci == g_overlay_sci && len == g_overlay_len) return;
    if (lang_for_path(g_last_active) == LANG_NONE) return;
    g_overlay_sci = sci;
    g_overlay_len = len;
    apply_token_overlay(sci);
}

// Fast 150ms hover check: shows the warning/error tooltip once the pointer has rested on a
// flagged line for VS Code's editor.hover.delay of 300ms.
static int hover_tick(void *data) {
    (void)data;
    detach_our_project_from_compiler();
    void *sci = get_active_scintilla();
    if (!sci || !fn_SendMsg) return 1;

    refresh_overlay_if_edited(sci);
    if (!fn_wxGetMousePos || !fn_ScreenToClient) return 1;

    if (fn_FindWindowAtPointer) {
        int pt[2];
        if (fn_FindWindowAtPointer(pt) != sci) {
            hide_hover(sci);
            return 1;
        }
    }

    int mx = 0, my = 0;
    fn_wxGetMousePos(&mx, &my);
    fn_ScreenToClient(sci, &mx, &my);

    long pos = fn_SendMsg(sci, SCI_POSITIONFROMPOINTCLOSE, (unsigned long)mx, (long)my);
    if (pos >= 0) {
        long line = fn_SendMsg(sci, SCI_LINEFROMPOSITION, (unsigned long)pos, 0);
        pthread_mutex_lock(&g_mutex);
        int found = -1;
        for (int i = 0; g_diag_sci == sci && i < g_diag_count; i++) {
            if (g_diags[i].line == (int)line) {
                found = i;
                break;
            }
        }
        if (found >= 0) {
            if (g_hover_candidate_line != (int)line) {
                g_hover_candidate_line = (int)line;
                g_hover_dwell = 0;
            }
            if (g_hover_dwell < 2) g_hover_dwell++;
            if (g_hover_dwell >= 2 && (!g_hover_calltip_active || g_last_hover_line != (int)line)) {
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

    hide_hover(sci);
    return 1;
}

// TEMP TEST HOOK: drive Build/Run in-process from /tmp/.cb_test_cmd
static void test_hook(void) {
    FILE *f = fopen("/tmp/.cb_test_cmd", "r");
    if (!f) return;
    char line[1200] = {0};
    if (!fgets(line, sizeof(line), f)) line[0] = 0;
    fclose(f);
    unlink("/tmp/.cb_test_cmd");
    char *nl = strchr(line, '\n'); if (nl) *nl = 0;
    resolve_cb_symbols();
    void *mgr = fn_Manager_Get ? fn_Manager_Get() : NULL;
    if (!mgr) return;
    dbg_log("TEST: cmd='%s'", line);
    if (!strncmp(line, "open ", 5)) {
        typedef void* (*open_fn)(void*, const void*, int, void*);
        open_fn op = (open_fn)dlsym(RTLD_DEFAULT, "_ZN13EditorManager4OpenERK8wxStringiP11ProjectFile");
        unsigned char wxstr[64]; memset(wxstr, 0, sizeof(wxstr));
        fn_cbC2U(wxstr, line + 5);
        void *r = op ? op(fn_GetEditorManager(mgr), wxstr, 0, NULL) : NULL;
        dbg_log("TEST: open -> %p", r);
        return;
    }
    typedef void* (*gpm_fn)(void*);
    typedef void* (*gfc_fn)(void*);
    gpm_fn gpm = (gpm_fn)dlsym(RTLD_DEFAULT, "_ZNK7Manager16GetPluginManagerEv");
    gfc_fn gfc = (gfc_fn)dlsym(RTLD_DEFAULT, "_ZNK13PluginManager16GetFirstCompilerEv");
    void *comp = (gpm && gfc) ? gfc(gpm(mgr)) : NULL;
    void *h = dlopen("/usr/lib/codeblocks/plugins/libcompiler.so", RTLD_NOW | RTLD_NOLOAD);
    typedef void (*ev_fn)(void*, void*);
    const char *sym = !strcmp(line, "buildrun") ? "_ZN11CompilerGCC15OnCompileAndRunER14wxCommandEvent"
                    : !strcmp(line, "run") ? "_ZN11CompilerGCC5OnRunER14wxCommandEvent" : NULL;
    ev_fn fn = (h && sym) ? (ev_fn)dlsym(h, sym) : NULL;
    dbg_log("TEST: comp=%p h=%p fn=%p active=%p", comp, h, (void*)fn,
            *(void**)((char*)fn_GetProjectManager(mgr) + 0x68));
    static char dummy[256];
    if (comp && fn) fn(comp, dummy);
}

static int periodic_tick(void *data) {
    (void)data;
    g_tick_counter++;
    test_hook();
    if (g_tick_counter >= 5) g_startup_ready = 1;
    void *sci = get_active_scintilla();
    if (!sci || !fn_SendMsg) return 1;

    if (editor_style_is_stale(sci)) apply_scintilla_opacity(sci);

    // Sync active file & auto-detect folder workspace on save/open
    char active_file[1024];
    int have_path = get_active_filepath(active_file, sizeof(active_file));
    if (have_path) sync_folder_workspace(active_file);

    // Diagnostics only make sense for C and C++, not Markdown, Python or unsaved tabs
    int lang = have_path ? lang_for_path(active_file) : LANG_NONE;
    if (lang == LANG_NONE) return 1;

    long len = fn_SendMsg(sci, SCI_GETLENGTH, 0, 0);
    if (len <= 0 || len > 250000) return 1;

    char *buf = (char*)malloc((size_t)len + 2);
    if (!buf) return 1;
    fn_SendMsg(sci, SCI_GETTEXT, (unsigned long)len + 1, (long)buf);
    uint64_t h = hash_str(buf, (size_t)len);

    // Catches same-length edits, which hover_tick's length check cannot see
    if (h != g_overlay_hash || sci != g_overlay_sci) {
        g_overlay_hash = h;
        g_overlay_sci = sci;
        g_overlay_len = len;
        apply_token_overlay(sci);
    }

    if ((h != g_last_hash || sci != g_last_sci) && g_lsp_dir[0] &&
        !__atomic_load_n(&g_checker_busy, __ATOMIC_ACQUIRE)) {
        CheckJob *job = (CheckJob*)malloc(sizeof(CheckJob));
        if (!job) {
            free(buf);
            return 1;
        }
        job->code = buf;
        job->sci = sci;
        job->lang = lang;
        snprintf(job->dir, sizeof(job->dir), "%s", active_file);
        char *slash = strrchr(job->dir, '/');
        if (slash) *slash = '\0';

        g_last_hash = h;
        g_last_sci = sci;
        __atomic_store_n(&g_checker_busy, 1, __ATOMIC_RELEASE);
        pthread_t tid;
        if (pthread_create(&tid, NULL, run_compiler_thread, job) == 0) {
            pthread_detach(tid);
        } else {
            free(job->code);
            free(job);
            __atomic_store_n(&g_checker_busy, 0, __ATOMIC_RELEASE);
        }
    } else {
        free(buf);
    }
    apply_diagnostics(sci);
    return 1;
}

static void launch_debugger(int asan) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "$HOME/.local/bin/cb_single_file_debug.sh %s &", asan ? "--asan" : "");
    int r = system(cmd);
    (void)r;
}

static void launch_cph_judge(void) {
    int r = system("$HOME/.local/bin/cb_cph_judge.py --run >/dev/null 2>&1 &");
    (void)r;
}

// ---- VS Code editor commands (src/vs/editor/contrib/{comment,linesOperations,multicursor}) ----

static void* focused_editor(void) {
    void *sci = get_active_scintilla();
    if (!sci || !fn_FindFocus || fn_FindFocus() != sci) return NULL;
    return sci;
}

static long line_from_pos(void *sci, long pos) {
    return fn_SendMsg(sci, SCI_LINEFROMPOSITION, (unsigned long)pos, 0);
}

static long line_start(void *sci, long line) {
    return fn_SendMsg(sci, SCI_POSITIONFROMLINE, (unsigned long)line, 0);
}

// A selection ending at column 0 does not include that last line, as in VS Code
static void selected_lines(void *sci, long *first, long *last) {
    long s = fn_SendMsg(sci, SCI_GETSELECTIONSTART, 0, 0);
    long e = fn_SendMsg(sci, SCI_GETSELECTIONEND, 0, 0);
    *first = line_from_pos(sci, s);
    *last = line_from_pos(sci, e);
    if (*last > *first && line_start(sci, *last) == e) (*last)--;
}

static int char_at(void *sci, long pos) {
    return (int)(unsigned char)fn_SendMsg(sci, SCI_GETCHARAT, (unsigned long)pos, 0);
}

static const char* eol_text(void *sci) {
    long mode = fn_SendMsg(sci, SCI_GETEOLMODE, 0, 0);
    return mode == 0 ? "\r\n" : (mode == 1 ? "\r" : "\n");
}

static const char* comment_token(void) {
    if (lang_for_path(g_last_active) != LANG_NONE) return "//";
    if (!strcmp(file_ext(g_last_active), ".py")) return "#";
    return NULL;
}

static int line_starts_with(void *sci, long pos, const char *token) {
    for (size_t i = 0; token[i]; i++) {
        if (char_at(sci, pos + (long)i) != (unsigned char)token[i]) return 0;
    }
    return 1;
}

// Ctrl+/ : same rule as lineCommentCommand.ts. Blank lines are ignored; if every other line
// is commented the token and one space are removed, otherwise "// " goes in at the lowest indent.
static void toggle_line_comment(void *sci) {
    const char *token = comment_token();
    if (!token) return;
    long first, last;
    selected_lines(sci, &first, &last);

    int all_commented = 1, any_code = 0;
    long min_indent = -1;
    for (long l = first; l <= last; l++) {
        long indent_pos = fn_SendMsg(sci, SCI_GETLINEINDENTPOSITION, (unsigned long)l, 0);
        if (indent_pos >= fn_SendMsg(sci, SCI_GETLINEENDPOSITION, (unsigned long)l, 0)) continue;
        any_code = 1;
        if (!line_starts_with(sci, indent_pos, token)) all_commented = 0;
        long indent = indent_pos - line_start(sci, l);
        if (min_indent < 0 || indent < min_indent) min_indent = indent;
    }
    if (!any_code) return;

    char insert[16];
    snprintf(insert, sizeof(insert), "%s ", token);
    long token_len = (long)strlen(token);

    fn_SendMsg(sci, SCI_BEGINUNDOACTION, 0, 0);
    for (long l = first; l <= last; l++) {
        long indent_pos = fn_SendMsg(sci, SCI_GETLINEINDENTPOSITION, (unsigned long)l, 0);
        if (indent_pos >= fn_SendMsg(sci, SCI_GETLINEENDPOSITION, (unsigned long)l, 0)) continue;
        if (all_commented) {
            long remove = token_len + (char_at(sci, indent_pos + token_len) == ' ' ? 1 : 0);
            fn_SendMsg(sci, SCI_DELETERANGE, (unsigned long)indent_pos, remove);
        } else {
            fn_SendMsg(sci, SCI_INSERTTEXT, (unsigned long)(line_start(sci, l) + min_indent), (long)insert);
        }
    }
    fn_SendMsg(sci, SCI_ENDUNDOACTION, 0, 0);
}

// Shift+Alt+Up / Down : copyLinesCommand.ts
static void copy_lines(void *sci, int down) {
    long anchor = fn_SendMsg(sci, SCI_GETANCHOR, 0, 0);
    long caret = fn_SendMsg(sci, SCI_GETCURRENTPOS, 0, 0);
    long first, last;
    selected_lines(sci, &first, &last);

    long start = line_start(sci, first);
    long line_count = fn_SendMsg(sci, SCI_GETLINECOUNT, 0, 0);
    long doc_len = fn_SendMsg(sci, SCI_GETLENGTH, 0, 0);
    int at_doc_end = (last + 1 >= line_count);
    long end = at_doc_end ? doc_len : line_start(sci, last + 1);

    const char *eol = eol_text(sci);
    size_t eol_len = strlen(eol);
    size_t block_len = (size_t)(end - start);
    char *block = (char*)malloc(block_len + eol_len * 2 + 1);
    if (!block) return;
    SciTextRange range = { start, end, block + eol_len };
    fn_SendMsg(sci, SCI_GETTEXTRANGE, 0, (long)&range);

    char *text = block + eol_len;
    if (at_doc_end && down) {
        memcpy(block, eol, eol_len);    // The last line has no newline to copy
        text = block;
    } else if (at_doc_end) {
        memcpy(block + eol_len + block_len, eol, eol_len + 1);
    }

    fn_SendMsg(sci, SCI_BEGINUNDOACTION, 0, 0);
    fn_SendMsg(sci, SCI_INSERTTEXT, (unsigned long)(down ? end : start), (long)text);
    long moved = (long)strlen(text);
    fn_SendMsg(sci, SCI_SETSEL, (unsigned long)(down ? anchor + moved : anchor), down ? caret + moved : caret);
    fn_SendMsg(sci, SCI_ENDUNDOACTION, 0, 0);
    free(block);
}

// Ctrl+Enter : insertLineAfter
static void insert_line_below(void *sci) {
    fn_SendMsg(sci, SCI_BEGINUNDOACTION, 0, 0);
    fn_SendMsg(sci, SCI_LINEEND, 0, 0);
    fn_SendMsg(sci, SCI_NEWLINE, 0, 0);
    fn_SendMsg(sci, SCI_ENDUNDOACTION, 0, 0);
}

// Ctrl+Shift+Enter : insertLineBefore, keeping the current line's indentation
static void insert_line_above(void *sci) {
    long line = line_from_pos(sci, fn_SendMsg(sci, SCI_GETCURRENTPOS, 0, 0));
    long indent = fn_SendMsg(sci, SCI_GETLINEINDENTATION, (unsigned long)line, 0);
    fn_SendMsg(sci, SCI_BEGINUNDOACTION, 0, 0);
    fn_SendMsg(sci, SCI_GOTOPOS, (unsigned long)line_start(sci, line), 0);
    fn_SendMsg(sci, SCI_NEWLINE, 0, 0);
    fn_SendMsg(sci, SCI_LINEUP, 0, 0);
    fn_SendMsg(sci, SCI_SETLINEINDENTATION, (unsigned long)line, indent);
    fn_SendMsg(sci, SCI_GOTOPOS, (unsigned long)fn_SendMsg(sci, SCI_GETLINEENDPOSITION, (unsigned long)line, 0), 0);
    fn_SendMsg(sci, SCI_ENDUNDOACTION, 0, 0);
}

// Ctrl+D : addSelectionToNextFindMatch. Case-sensitive across the whole file, as in VS Code.
static void add_next_occurrence(void *sci) {
    long flags = fn_SendMsg(sci, SCI_GETSEARCHFLAGS, 0, 0);
    fn_SendMsg(sci, SCI_SETSEARCHFLAGS, SCFIND_MATCHCASE, 0);
    fn_SendMsg(sci, SCI_TARGETWHOLEDOCUMENT, 0, 0);
    fn_SendMsg(sci, SCI_MULTIPLESELECTADDNEXT, 0, 0);
    fn_SendMsg(sci, SCI_SETSEARCHFLAGS, (unsigned long)flags, 0);
}

static int run_editor_command(GdkEventKey_t *event) {
    int ctrl  = (event->state & 4) != 0;
    int shift = (event->state & 1) != 0;
    int alt   = (event->state & 8) != 0;
    uint32_t key = event->keyval;
    int enter = (key == 0xff0d || key == 0xff8d);
    int up_down = (key == 0xff52 || key == 0xff54);

    int wanted = (ctrl && !alt && !shift && (key == '/' || key == 'd' || key == 'D')) ||
                 (ctrl && !alt && enter) ||
                 (alt && shift && !ctrl && up_down);
    if (!wanted) return 0;

    void *sci = focused_editor();
    if (!sci) return 0;

    if (key == '/') toggle_line_comment(sci);
    else if (key == 'd' || key == 'D') add_next_occurrence(sci);
    else if (enter && shift) insert_line_above(sci);
    else if (enter) insert_line_below(sci);
    else copy_lines(sci, key == 0xff54);
    return 1;
}

static int my_key_snooper(void *grab_widget, GdkEventKey_t *event, void *func_data) {
    (void)grab_widget;
    (void)func_data;
    if (!event) return 0;

    if (event->type == 8) detach_our_project_from_compiler();

    // F5 (0xffc2) = Single-File Visual GDB Debugger, Ctrl+F5 = AddressSanitizer
    if (event->type == 8 && event->keyval == 0xffc2) {
        int ctrl = (event->state & 4) != 0;
        launch_debugger(ctrl);
        return 1;
    }

    // F6 (0xffc3) or Ctrl+Alt+B = CPH Judge: Run All Testcases
    if (event->type == 8 && (event->keyval == 0xffc3 ||
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

    if (event->type == 8 && run_editor_command(event)) return 1;

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

// The launcher preloads this library, and children must not inherit that: ASan refuses to run
// behind a preload, and any GTK child (e.g. the debugger's terminal) would get our key snooper.
static void remove_self_from_ld_preload(void) {
    const char *value = getenv("LD_PRELOAD");
    if (!value) return;
    char kept[4096] = "";
    char copy[4096];
    snprintf(copy, sizeof(copy), "%s", value);
    char *save = NULL;
    for (char *tok = strtok_r(copy, ": ", &save); tok; tok = strtok_r(NULL, ": ", &save)) {
        if (strstr(tok, "libvscode_helper")) continue;
        if (kept[0]) strncat(kept, ":", sizeof(kept) - strlen(kept) - 1);
        strncat(kept, tok, sizeof(kept) - strlen(kept) - 1);
    }
    if (kept[0]) setenv("LD_PRELOAD", kept, 1);
    else unsetenv("LD_PRELOAD");
}

static int running_in_codeblocks(void) {
    char exe[1024];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n <= 0) return 0;
    exe[n] = '\0';
    const char *base = strrchr(exe, '/');
    return strcmp(base ? base + 1 : exe, "codeblocks") == 0;
}

__attribute__((constructor))
static void init_vscode_helper(void) {
    remove_self_from_ld_preload();
    if (!running_in_codeblocks()) return;

    snprintf(g_lsp_dir, sizeof(g_lsp_dir), "/tmp/cb_lsp_XXXXXX");
    if (!mkdtemp(g_lsp_dir)) g_lsp_dir[0] = '\0';

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

__attribute__((destructor))
static void cleanup_vscode_helper(void) {
    if (!g_lsp_dir[0]) return;
    char path[128];
    snprintf(path, sizeof(path), "%s/live.c", g_lsp_dir);
    unlink(path);
    snprintf(path, sizeof(path), "%s/live.cpp", g_lsp_dir);
    unlink(path);
    rmdir(g_lsp_dir);
}
