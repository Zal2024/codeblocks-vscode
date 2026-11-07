// Code::Blocks plugin: a toolbar for the VS Code Edition tools, plus a controls reference.
#include <sdk.h>

#include <cbeditor.h>
#include <cbplugin.h>
#include <configmanager.h>
#include <editormanager.h>
#include <logmanager.h>
#include <manager.h>
#include <sdk_events.h>

#include <wx/aui/aui.h>
#include <wx/button.h>
#include <wx/dialog.h>
#include <wx/dynlib.h>
#include <wx/image.h>
#include <wx/mstream.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/toolbar.h>
#include <wx/utils.h>

#include <dlfcn.h>

#include "vscode_toolbar_icons.h"

namespace {

wxBitmap LoadIcon(const char *name, int size) {
    // Use the closest rendered size at or above the request, then scale down if needed
    const ToolbarIcon *best = nullptr;
    for (const ToolbarIcon &icon : kToolbarIcons) {
        if (strcmp(icon.name, name) != 0) continue;
        if (!best || (icon.size >= size && (best->size < size || icon.size < best->size)))
            best = &icon;
    }
    if (!best) return wxBitmap();
    wxMemoryInputStream stream(best->png, best->len);
    wxImage image(stream, wxBITMAP_TYPE_PNG);
    if (image.IsOk() && image.GetWidth() != size) image.Rescale(size, size, wxIMAGE_QUALITY_HIGH);
    return wxBitmap(image);
}

wxString ActiveFile() {
    EditorBase *editor = Manager::Get()->GetEditorManager()->GetActiveEditor();
    if (!editor) return wxString();
    wxString path = editor->GetFilename();
    return wxIsAbsolutePath(path) ? path : wxString();
}

void Launch(const wxString &script, const wxString &flag) {
    wxString path = wxGetHomeDir() + wxT("/.local/bin/") + script;
    wxString file = ActiveFile();
    const wxChar *argv[4] = { path.wc_str(), nullptr, nullptr, nullptr };
    int n = 1;
    if (!flag.empty()) argv[n++] = flag.wc_str();
    if (!file.empty()) argv[n++] = file.wc_str();
    if (wxExecute(const_cast<wxChar **>(argv), wxEXEC_ASYNC) <= 0)
        wxMessageBox(wxT("Could not start ") + path, wxT("VS Code Toolbar"), wxICON_ERROR);
}

struct Shortcut { const wxChar *keys; const wxChar *action; };
struct Section { const wxChar *title; const Shortcut *items; size_t count; };

const Shortcut kRun[] = {
    { wxT("F6  or  Ctrl+Alt+B"), wxT("CPH Judge: open the testcase panel and run every testcase") },
    { wxT("F5"),                 wxT("Debug the active file in GDB (no project needed)") },
    { wxT("Ctrl+F5"),            wxT("Run the active file with AddressSanitizer + UBSan") },
};
const Shortcut kEdit[] = {
    { wxT("Ctrl+/"),                         wxT("Toggle line comment") },
    { wxT("Ctrl+D"),                         wxT("Add the next occurrence to the selection") },
    { wxT("Alt+Up / Down"),                  wxT("Move line up / down") },
    { wxT("Shift+Alt+Up / Down"),            wxT("Copy line up / down") },
    { wxT("Ctrl+Enter / Ctrl+Shift+Enter"),  wxT("Insert line below / above") },
};
const Shortcut kMove[] = {
    { wxT("Ctrl+I / K / J / L"),       wxT("Cursor up / down / left / right") },
    { wxT("Ctrl+Shift+I / K / J / L"), wxT("Select up / down / left / right") },
    { wxT("Ctrl+Alt+J / L"),           wxT("Jump one word left / right (add Shift to select)") },
};
const Shortcut kDiag[] = {
    { wxT("Red squiggle"),     wxT("Compiler error: rest the mouse on it to read the message") },
    { wxT("Yellow squiggle"),  wxT("Warning: rest the mouse on it to read the message") },
    { wxT("Ctrl+Shift+M"),     wxT("Show every message in a box under its line (toggle)") },
};
const Shortcut kCph[] = {
    { wxT("1."), wxT("Open or save a .c / .cpp file") },
    { wxT("2."), wxT("Press F6 or the beaker button: the CPH Judge panel opens beside the editor") },
    { wxT("3."), wxT("Type each testcase's input and expected output, or use + New Testcase") },
    { wxT("4."), wxT("Press F6 again after editing to rerun everything") },
    { wxT(""),   wxT("Testcases are saved in .cph/ next to your file, like the VS Code extension") },
};
const Section kSections[] = {
    { wxT("Run && Debug"), kRun, WXSIZEOF(kRun) },
    { wxT("Editing"), kEdit, WXSIZEOF(kEdit) },
    { wxT("Cursor movement"), kMove, WXSIZEOF(kMove) },
    { wxT("Errors && warnings"), kDiag, WXSIZEOF(kDiag) },
    { wxT("Using CPH Judge"), kCph, WXSIZEOF(kCph) },
};

class ControlsDialog : public wxDialog {
public:
    explicit ControlsDialog(wxWindow *parent)
        : wxDialog(parent, wxID_ANY, wxT("Controls & Instructions"), wxDefaultPosition, wxDefaultSize,
                   wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER) {
        wxScrolledWindow *body = new wxScrolledWindow(this, wxID_ANY);
        wxBoxSizer *column = new wxBoxSizer(wxVERTICAL);
        wxFont bold = body->GetFont().Bold();
        wxFont heading = bold.Larger();

        for (const Section &section : kSections) {
            wxStaticText *title = new wxStaticText(body, wxID_ANY, section.title);
            title->SetFont(heading);
            column->Add(title, 0, wxTOP | wxLEFT | wxRIGHT, 12);
            wxFlexGridSizer *grid = new wxFlexGridSizer(2, 4, 18);
            for (size_t i = 0; i < section.count; ++i) {
                wxStaticText *keys = new wxStaticText(body, wxID_ANY, section.items[i].keys);
                keys->SetFont(bold);
                grid->Add(keys, 0, wxALIGN_TOP);
                grid->Add(new wxStaticText(body, wxID_ANY, section.items[i].action), 0, wxALIGN_TOP);
            }
            column->Add(grid, 0, wxALL, 8);
        }
        body->SetSizer(column);
        body->SetScrollRate(0, 12);
        column->Fit(body);

        wxBoxSizer *outer = new wxBoxSizer(wxVERTICAL);
        outer->Add(body, 1, wxEXPAND);
        outer->Add(CreateButtonSizer(wxCLOSE), 0, wxEXPAND | wxALL, 8);
        SetSizer(outer);
        SetEscapeId(wxID_CLOSE);
        wxSize best = column->GetMinSize();
        SetClientSize(best.x + 24, wxMin(best.y + 60, 640));
        CentreOnParent();
    }
};

} // namespace

class VSCodeToolbar : public cbPlugin {
public:
    bool BuildToolBar(wxToolBar *toolBar) override {
        if (!toolBar) return false;
        int size = Manager::Get()->GetImageSize(Manager::UIComponent::Toolbars);
        if (size <= 0) size = 16;
        AddTool(toolBar, m_IdCph, "beaker", size, wxT("Run testcases (CPH Judge)"),
                wxT("F6 / Ctrl+Alt+B: open CPH Judge and run all testcases for the active file"));
        AddTool(toolBar, m_IdDebug, "debug_alt", size, wxT("Debug active file"),
                wxT("F5: compile with -g3 and debug the active file in GDB"));
        AddTool(toolBar, m_IdAsan, "shield", size, wxT("Run with sanitizers"),
                wxT("Ctrl+F5: run the active file with AddressSanitizer + UBSan"));
        toolBar->AddSeparator();
        AddTool(toolBar, m_IdWarnings, "eye", size, wxT("Show messages inline"),
                wxT("Ctrl+Shift+M: show error and warning messages under their lines, not just on hover"));
        AddTool(toolBar, m_IdHelp, "question", size, wxT("Controls & instructions"),
                wxT("Every shortcut and how to use CPH Judge"));
        toolBar->Realize();
        m_ToolBar = toolBar;
        return true;
    }

protected:
    void OnAttach() override {
        m_IdCph = wxWindow::NewControlId();
        m_IdDebug = wxWindow::NewControlId();
        m_IdAsan = wxWindow::NewControlId();
        m_IdWarnings = wxWindow::NewControlId();
        m_IdHelp = wxWindow::NewControlId();
        Bind(wxEVT_TOOL, [](wxCommandEvent &) { Launch(wxT("cb_cph_judge.py"), wxT("--run")); }, m_IdCph);
        Bind(wxEVT_TOOL, [](wxCommandEvent &) { Launch(wxT("cb_single_file_debug.sh"), wxString()); }, m_IdDebug);
        Bind(wxEVT_TOOL, [](wxCommandEvent &) { Launch(wxT("cb_single_file_debug.sh"), wxT("--asan")); }, m_IdAsan);
        Bind(wxEVT_TOOL, &VSCodeToolbar::OnToggleWarnings, this, m_IdWarnings);
        Bind(wxEVT_UPDATE_UI, &VSCodeToolbar::OnUpdateWarnings, this, m_IdWarnings);
        Bind(wxEVT_TOOL, [](wxCommandEvent &) {
            ControlsDialog dialog(Manager::Get()->GetAppWindow());
            dialog.ShowModal();
        }, m_IdHelp);
        Manager::Get()->RegisterEventSink(cbEVT_APP_STARTUP_DONE,
            new cbEventFunctor<VSCodeToolbar, CodeBlocksEvent>(this, &VSCodeToolbar::OnStartupDone));
        Manager::Get()->RegisterEventSink(cbEVT_APP_START_SHUTDOWN,
            new cbEventFunctor<VSCodeToolbar, CodeBlocksEvent>(this, &VSCodeToolbar::OnShutdown));
    }

private:
    typedef void (*ToggleFn)(void);

    wxAuiPaneInfo *OurPane(wxAuiManager **aui) {
        if (!m_ToolBar) return nullptr;
        *aui = wxAuiManager::GetManager(m_ToolBar);
        if (!*aui) return nullptr;
        wxAuiPaneInfo &pane = (*aui)->GetPane(m_ToolBar);
        return pane.IsOk() ? &pane : nullptr;
    }

    // A saved layout hides toolbars it does not know, and the "Layout changed" prompt
    // defaults to No, so ours is often missing from it. Show it beside the build toolbar
    // on every start unless the user hid it (View > Toolbars) in the previous session.
    void OnStartupDone(CodeBlocksEvent &event) {
        event.Skip();
        ConfigManager *cfg = Manager::Get()->GetConfigManager(wxT("vscode_toolbar"));
        wxAuiManager *aui = nullptr;
        wxAuiPaneInfo *pane = OurPane(&aui);
        if (!pane || pane->IsShown() || !cfg->ReadBool(wxT("/visible"), true)) return;

        const wxAuiPaneInfo *anchor = nullptr;
        wxAuiPaneInfoArray &panes = aui->GetAllPanes();
        for (size_t i = 0; i < panes.GetCount(); ++i) {
            const wxAuiPaneInfo &p = panes[i];
            if (p.window == m_ToolBar || !p.IsToolbar() || !p.IsShown()) continue;
            if (p.name.Lower().Contains(wxT("compiler"))) anchor = &p;
        }
        if (anchor) {
            pane->Direction(anchor->dock_direction).Layer(anchor->dock_layer).Row(anchor->dock_row)
                .Position(anchor->dock_pos + 1);
        }
        pane->Show();
        aui->Update();
        Manager::Get()->GetLogManager()->DebugLog(wxT("vscode_toolbar: shown beside ")
                                                  + (anchor ? anchor->name : wxString(wxT("(none)"))));
    }

    void OnShutdown(CodeBlocksEvent &event) {
        event.Skip();
        wxAuiManager *aui = nullptr;
        if (wxAuiPaneInfo *pane = OurPane(&aui))
            Manager::Get()->GetConfigManager(wxT("vscode_toolbar"))->Write(wxT("/visible"), pane->IsShown());
    }

    static ToggleFn HelperToggle() {
        // Exported by libvscode_helper.so, which owns the diagnostics
        return reinterpret_cast<ToggleFn>(dlsym(RTLD_DEFAULT, "vscode_helper_toggle_inline_warnings"));
    }

    void OnToggleWarnings(wxCommandEvent &) {
        if (ToggleFn toggle = HelperToggle()) toggle();
    }

    void OnUpdateWarnings(wxUpdateUIEvent &event) {
        event.Enable(HelperToggle() != nullptr);
    }

    void AddTool(wxToolBar *toolBar, int id, const char *icon, int size, const wxString &label,
                 const wxString &help) {
        toolBar->AddTool(id, label, LoadIcon(icon, size), label + wxT("  (") + help.BeforeFirst(':') + wxT(")"),
                         wxITEM_NORMAL);
        toolBar->SetToolLongHelp(id, help);
    }

    int m_IdCph = wxID_ANY;
    int m_IdDebug = wxID_ANY;
    int m_IdAsan = wxID_ANY;
    int m_IdWarnings = wxID_ANY;
    int m_IdHelp = wxID_ANY;
    wxToolBar *m_ToolBar = nullptr;
};

namespace {
PluginRegistrant<VSCodeToolbar> reg(wxT("vscode_toolbar"));
}
