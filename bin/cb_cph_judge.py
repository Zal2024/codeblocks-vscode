#!/usr/bin/env python3
import gi
gi.require_version('Gtk', '3.0')
from gi.repository import Gtk, Gdk, GLib, Pango
import hashlib
import json
import os
import subprocess
import sys
import time
import threading

CSS = b"""
window, .main-bg {
    background-color: #1E1E1E;
    color: #CCCCCC;
    font-family: "Segoe UI", "Ubuntu", sans-serif;
    font-size: 13px;
}
.header-bar {
    background-color: #252526;
    border-bottom: 1px solid #333333;
    padding: 8px 12px;
}
.title-label {
    color: #E0E0E0;
    font-weight: bold;
    font-size: 13px;
}
.problem-title {
    color: #FFFFFF;
    font-weight: bold;
    font-size: 16px;
}
.badge-neutral {
    background-color: #3A3D41;
    color: #CCCCCC;
    border-radius: 10px;
    padding: 2px 10px;
    font-size: 12px;
}
.badge-pass {
    background-color: #1E4620;
    color: #89D185;
    border-radius: 10px;
    padding: 2px 10px;
    font-size: 12px;
    font-weight: bold;
}
.badge-fail {
    background-color: #5A1D1D;
    color: #F48771;
    border-radius: 10px;
    padding: 2px 10px;
    font-size: 12px;
    font-weight: bold;
}
.tc-card {
    background-color: #252526;
    border: 1px solid #333333;
    border-radius: 4px;
    padding: 8px 10px;
    margin-bottom: 8px;
}
.tc-header {
    color: #3794FF;
    font-weight: bold;
    font-size: 14px;
}
.field-label {
    color: #AAAAAA;
    font-size: 12px;
}
textview, textview text {
    background-color: #181818;
    color: #D4D4D4;
    font-family: "JetBrains Mono", "DejaVu Sans Mono", monospace;
    font-size: 12px;
}
.io-frame {
    border: 1px solid #3C3C3C;
    border-radius: 3px;
    background-color: #181818;
}
.io-frame-pass {
    border: 1px solid #388A34;
    border-radius: 3px;
    background-color: #181818;
}
.io-frame-fail {
    border: 1px solid #C72E0F;
    border-radius: 3px;
    background-color: #181818;
}
button {
    background-image: none;
    box-shadow: none;
    text-shadow: none;
    border-radius: 3px;
    border: 1px solid #3C3C3C;
    background-color: #2D2D2D;
    color: #CCCCCC;
    padding: 4px 10px;
}
button:hover {
    background-color: #383838;
}
.btn-green {
    background-color: #388A34;
    color: #FFFFFF;
    border-color: #388A34;
    font-weight: bold;
    padding: 6px 12px;
}
.btn-green:hover {
    background-color: #45A041;
}
.btn-blue {
    background-color: #0E639C;
    color: #FFFFFF;
    border-color: #0E639C;
    font-weight: bold;
    padding: 6px 12px;
}
.btn-blue:hover {
    background-color: #1177BB;
}
.btn-red {
    background-color: #A1260D;
    color: #FFFFFF;
    border-color: #A1260D;
    font-weight: bold;
    padding: 4px 10px;
}
.btn-red:hover {
    background-color: #C72E0F;
}
.btn-copy {
    background-color: transparent;
    border: none;
    color: #888888;
    font-size: 11px;
    padding: 1px 6px;
}
.btn-copy:hover {
    color: #FFFFFF;
}
"""

def get_prob_path(src_path):
    d = os.path.dirname(src_path)
    base = os.path.basename(src_path)
    cph_dir = os.path.join(d, ".cph")
    os.makedirs(cph_dir, exist_ok=True)
    h = hashlib.md5(src_path.encode("utf-8")).hexdigest()
    return os.path.join(cph_dir, f".{base}_{h}.prob")

def detect_initial_file():
    if len(sys.argv) > 1 and os.path.isfile(sys.argv[1]):
        return os.path.abspath(sys.argv[1])
    if os.path.isfile("/tmp/.cb_active_file"):
        try:
            with open("/tmp/.cb_active_file", "r") as f:
                p = f.read().strip()
            if os.path.isfile(p):
                return p
        except Exception:
            pass
    # Fallback: newest .c / .cpp in ~/Documents
    best_p, best_t = None, -1
    docs = os.path.expanduser("~/Documents")
    for root, _, files in os.walk(docs):
        if "codeblocks-vscode" in root:
            continue
        for fn in files:
            if fn.endswith((".c", ".cpp")):
                fp = os.path.join(root, fn)
                try:
                    t = os.path.getmtime(fp)
                    if t > best_t:
                        best_t, best_p = t, fp
                except Exception:
                    pass
    return best_p

class TestCaseRow:
    def __init__(self, app, idx, tc_data):
        self.app = app
        self.idx = idx
        self.tc_data = tc_data
        self.expanded = True

        self.card = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)
        self.card.get_style_context().add_class("tc-card")

        # Header row
        hbar = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=6)
        self.toggle_btn = Gtk.Button(label=f"⌄ TC {idx + 1}")
        self.toggle_btn.get_style_context().add_class("btn-copy")
        self.toggle_btn.get_style_context().add_class("tc-header")
        self.toggle_btn.connect("clicked", self.on_toggle)
        hbar.pack_start(self.toggle_btn, False, False, 0)

        self.status_lbl = Gtk.Label(label="")
        hbar.pack_start(self.status_lbl, False, False, 4)

        del_btn = Gtk.Button(label="🗑")
        del_btn.get_style_context().add_class("btn-red")
        del_btn.set_tooltip_text("Delete Testcase")
        del_btn.connect("clicked", lambda *_: self.app.delete_testcase(self.idx))
        hbar.pack_end(del_btn, False, False, 0)

        run_btn = Gtk.Button(label="▷")
        run_btn.get_style_context().add_class("btn-green")
        run_btn.set_tooltip_text("Run Testcase")
        run_btn.connect("clicked", lambda *_: self.app.run_single_testcase(self.idx))
        hbar.pack_end(run_btn, False, False, 0)

        self.card.pack_start(hbar, False, False, 0)

        # Body
        self.body = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)

        # Input section
        in_hdr = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL)
        in_lbl = Gtk.Label(label="Input:")
        in_lbl.get_style_context().add_class("field-label")
        in_hdr.pack_start(in_lbl, False, False, 0)
        in_copy = Gtk.Button(label="Copy")
        in_copy.get_style_context().add_class("btn-copy")
        in_copy.connect("clicked", lambda *_: self.copy_buf(self.in_buf))
        in_hdr.pack_end(in_copy, False, False, 0)
        self.body.pack_start(in_hdr, False, False, 0)

        self.in_buf = Gtk.TextBuffer()
        self.in_buf.set_text(tc_data.get("input", ""))
        self.in_buf.connect("changed", self.on_text_changed)
        in_tv = Gtk.TextView(buffer=self.in_buf)
        in_tv.set_Left_margin(6) if hasattr(in_tv, "set_Left_margin") else in_tv.set_left_margin(6)
        in_tv.set_right_margin(6)
        in_tv.set_top_margin(4)
        in_tv.set_bottom_margin(4)
        in_frame = Gtk.Frame()
        in_frame.get_style_context().add_class("io-frame")
        in_frame.add(in_tv)
        self.body.pack_start(in_frame, False, False, 0)

        # Expected Output section
        exp_hdr = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL)
        exp_lbl = Gtk.Label(label="Expected Output:")
        exp_lbl.get_style_context().add_class("field-label")
        exp_hdr.pack_start(exp_lbl, False, False, 0)
        exp_copy = Gtk.Button(label="Copy")
        exp_copy.get_style_context().add_class("btn-copy")
        exp_copy.connect("clicked", lambda *_: self.copy_buf(self.exp_buf))
        exp_hdr.pack_end(exp_copy, False, False, 0)
        self.body.pack_start(exp_hdr, False, False, 0)

        self.exp_buf = Gtk.TextBuffer()
        self.exp_buf.set_text(tc_data.get("output", ""))
        self.exp_buf.connect("changed", self.on_text_changed)
        exp_tv = Gtk.TextView(buffer=self.exp_buf)
        exp_tv.set_left_margin(6)
        exp_tv.set_right_margin(6)
        exp_tv.set_top_margin(4)
        exp_tv.set_bottom_margin(4)
        exp_frame = Gtk.Frame()
        exp_frame.get_style_context().add_class("io-frame")
        exp_frame.add(exp_tv)
        self.body.pack_start(exp_frame, False, False, 0)

        # Actual Output section (shown after running)
        self.out_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=4)
        out_hdr = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL)
        out_lbl = Gtk.Label(label="Received Output:")
        out_lbl.get_style_context().add_class("field-label")
        out_hdr.pack_start(out_lbl, False, False, 0)
        out_copy = Gtk.Button(label="Copy")
        out_copy.get_style_context().add_class("btn-copy")
        out_copy.connect("clicked", lambda *_: self.copy_buf(self.out_buf))
        out_hdr.pack_end(out_copy, False, False, 0)
        self.out_box.pack_start(out_hdr, False, False, 0)

        self.out_buf = Gtk.TextBuffer()
        out_tv = Gtk.TextView(buffer=self.out_buf)
        out_tv.set_editable(False)
        out_tv.set_left_margin(6)
        out_tv.set_right_margin(6)
        out_tv.set_top_margin(4)
        out_tv.set_bottom_margin(4)
        self.out_frame = Gtk.Frame()
        self.out_frame.get_style_context().add_class("io-frame")
        self.out_frame.add(out_tv)
        self.out_box.pack_start(self.out_frame, False, False, 0)
        self.body.pack_start(self.out_box, False, False, 0)

        self.card.pack_start(self.body, False, False, 0)

    def on_toggle(self, *_):
        self.expanded = not self.expanded
        self.toggle_btn.set_label(f"{'⌄' if self.expanded else '›'} TC {self.idx + 1}")
        if self.expanded:
            self.body.show_all()
        else:
            self.body.hide()

    def copy_buf(self, buf):
        start, end = buf.get_bounds()
        txt = buf.get_text(start, end, True)
        cb = Gtk.Clipboard.get(Gdk.SELECTION_CLIPBOARD)
        cb.set_text(txt, -1)

    def on_text_changed(self, *_):
        s1, e1 = self.in_buf.get_bounds()
        s2, e2 = self.exp_buf.get_bounds()
        self.tc_data["input"] = self.in_buf.get_text(s1, e1, True)
        self.tc_data["output"] = self.exp_buf.get_text(s2, e2, True)
        self.app.save_prob()

    def set_result(self, status, actual_text, ms):
        ctx = self.status_lbl.get_style_context()
        ctx.remove_class("badge-pass")
        ctx.remove_class("badge-fail")
        fctx = self.out_frame.get_style_context()
        fctx.remove_class("io-frame-pass")
        fctx.remove_class("io-frame-fail")

        if status == "PASS":
            self.status_lbl.set_text(f"PASSED ({ms} ms)")
            ctx.add_class("badge-pass")
            fctx.add_class("io-frame-pass")
        else:
            self.status_lbl.set_text(f"{status} ({ms} ms)")
            ctx.add_class("badge-fail")
            fctx.add_class("io-frame-fail")
        self.out_buf.set_text(actual_text)
        self.out_box.show_all()


class CPHJudgeWindow(Gtk.Window):
    def __init__(self):
        super().__init__(title="CPH Judge: Results")
        self.set_default_size(370, 680)
        self.set_keep_above(True)

        # Position on the right side of the screen like VS Code's CPH sidebar
        screen = Gdk.Screen.get_default()
        if screen:
            sw = screen.get_width()
            sh = screen.get_height()
            self.move(max(0, sw - 385), 45)

        provider = Gtk.CssProvider()
        provider.load_from_data(CSS)
        Gtk.StyleContext.add_provider_for_screen(
            Gdk.Screen.get_default(), provider, Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION
        )

        self.src_path = None
        self.prob_data = {}
        self.rows = []
        self.last_trigger_mtime = 0

        root = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=0)
        root.get_style_context().add_class("main-bg")
        self.add(root)

        # Top Title Bar
        top_bar = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL)
        top_bar.get_style_context().add_class("header-bar")
        hdr_lbl = Gtk.Label(label="CPH Judge: Results")
        hdr_lbl.get_style_context().add_class("title-label")
        top_bar.pack_start(hdr_lbl, False, False, 0)
        root.pack_start(top_bar, False, False, 0)

        # Problem Header & Pass Count Badge
        prob_bar = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=8)
        prob_bar.set_margin_top(10)
        prob_bar.set_margin_bottom(6)
        prob_bar.set_margin_start(12)
        prob_bar.set_margin_end(12)

        self.prob_lbl = Gtk.Label(label="Local: None")
        self.prob_lbl.get_style_context().add_class("problem-title")
        prob_bar.pack_start(self.prob_lbl, False, False, 0)

        self.pass_badge = Gtk.Label(label="0 / 0 passed")
        self.pass_badge.get_style_context().add_class("badge-neutral")
        prob_bar.pack_end(self.pass_badge, False, False, 0)
        root.pack_start(prob_bar, False, False, 0)

        # Scrollable Testcases Container
        scroll = Gtk.ScrolledWindow()
        scroll.set_policy(Gtk.PolicyType.NEVER, Gtk.PolicyType.AUTOMATIC)
        scroll.set_margin_start(10)
        scroll.set_margin_end(10)
        self.tc_list_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=8)
        scroll.add(self.tc_list_box)
        root.pack_start(scroll, True, True, 4)

        # Bottom Controls (matching VS Code CPH layout)
        bot = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=8)
        bot.set_margin_start(12)
        bot.set_margin_end(12)
        bot.set_margin_top(8)
        bot.set_margin_bottom(12)

        new_tc_btn = Gtk.Button(label="＋ New Testcase")
        new_tc_btn.get_style_context().add_class("btn-green")
        new_tc_btn.connect("clicked", self.on_new_testcase)
        bot.pack_start(new_tc_btn, False, False, 0)

        run_all_btn = Gtk.Button(label="▷ Run All Testcases (Ctrl+Alt+B)")
        run_all_btn.get_style_context().add_class("btn-blue")
        run_all_btn.connect("clicked", lambda *_: self.run_all_testcases())
        bot.pack_start(run_all_btn, False, False, 0)

        self.oj_check = Gtk.CheckButton(label="Set ONLINE_JUDGE")
        self.oj_check.set_active(True)
        bot.pack_start(self.oj_check, False, False, 2)

        root.pack_start(bot, False, False, 0)

        # Load initial file
        init_f = detect_initial_file()
        if init_f:
            self.load_file(init_f)

        # Poll /tmp/.cb_active_file and /tmp/.cb_cph_run_trigger every 350ms
        GLib.timeout_add(350, self.poll_active_file)

    def load_file(self, path):
        if not path or not os.path.isfile(path):
            return
        if not path.endswith((".c", ".cpp", ".cc", ".cxx", ".py")):
            return
        self.src_path = path
        stem = os.path.splitext(os.path.basename(path))[0]
        self.prob_lbl.set_text(f"Local: {stem}")

        pfile = get_prob_path(path)
        if os.path.isfile(pfile):
            try:
                with open(pfile, "r", encoding="utf-8") as f:
                    self.prob_data = json.load(f)
            except Exception:
                self.prob_data = {}
        else:
            self.prob_data = {}

        if "tests" not in self.prob_data or not isinstance(self.prob_data["tests"], list) or len(self.prob_data["tests"]) == 0:
            self.prob_data = {
                "name": f"Local: {stem}",
                "url": path,
                "tests": [{"id": int(time.time() * 1000), "input": "", "output": ""}],
                "interactive": False,
                "memoryLimit": 1024,
                "timeLimit": 3000,
                "srcPath": path,
                "group": "local",
                "local": True,
            }
            self.save_prob()

        self.rebuild_tc_ui()

    def save_prob(self):
        if not self.src_path:
            return
        pfile = get_prob_path(self.src_path)
        try:
            with open(pfile, "w", encoding="utf-8") as f:
                json.dump(self.prob_data, f)
        except Exception:
            pass

    def rebuild_tc_ui(self):
        for child in self.tc_list_box.get_children():
            self.tc_list_box.remove(child)
        self.rows = []
        tests = self.prob_data.get("tests", [])
        for i, tc in enumerate(tests):
            row = TestCaseRow(self, i, tc)
            self.rows.append(row)
            self.tc_list_box.pack_start(row.card, False, False, 0)
        self.tc_list_box.show_all()
        for r in self.rows:
            r.out_box.hide()
        self.pass_badge.set_text(f"0 / {len(tests)} passed")

    def on_new_testcase(self, *_):
        if "tests" not in self.prob_data:
            self.prob_data["tests"] = []
        self.prob_data["tests"].append({"id": int(time.time() * 1000), "input": "", "output": ""})
        self.save_prob()
        self.rebuild_tc_ui()

    def delete_testcase(self, idx):
        tests = self.prob_data.get("tests", [])
        if 0 <= idx < len(tests):
            tests.pop(idx)
            self.save_prob()
            self.rebuild_tc_ui()

    def compile_source(self):
        if not self.src_path:
            return None, "No active source file."
        if self.src_path.endswith(".py"):
            return ["python3", self.src_path], None
        ext = os.path.splitext(self.src_path)[1]
        cc = "g++" if ext in (".cpp", ".cc", ".cxx") else "gcc"
        out_bin = f"/tmp/cph_bin_{os.getuid()}"
        cmd = [cc, "-O2", "-Wall", self.src_path, "-o", out_bin, "-lm"]
        if self.oj_check.get_active():
            cmd.insert(2, "-DONLINE_JUDGE")
        res = subprocess.run(cmd, capture_output=True, text=True)
        if res.returncode != 0:
            return None, res.stderr.strip()
        return [out_bin], None

    def execute_tc(self, run_cmd, inp):
        t0 = time.perf_counter()
        try:
            proc = subprocess.run(
                run_cmd,
                input=inp,
                capture_output=True,
                text=True,
                timeout=3.0,
            )
            ms = int((time.perf_counter() - t0) * 1000)
            if proc.returncode != 0:
                return "RE", (proc.stdout + "\n" + proc.stderr).strip(), ms
            return "OK", proc.stdout, ms
        except subprocess.TimeoutExpired:
            ms = int((time.perf_counter() - t0) * 1000)
            return "TLE", "Time Limit Exceeded (3.0s)", ms

    @staticmethod
    def normalize_out(s):
        return "\n".join(line.rstrip() for line in s.strip().splitlines())

    def run_single_testcase(self, idx):
        if idx < 0 or idx >= len(self.rows):
            return
        run_cmd, err = self.compile_source()
        row = self.rows[idx]
        if err:
            row.set_result("CE", err, 0)
            return
        inp = row.tc_data.get("input", "")
        exp = row.tc_data.get("output", "")
        st, out, ms = self.execute_tc(run_cmd, inp)
        if st != "OK":
            row.set_result(st, out, ms)
        else:
            if self.normalize_out(out) == self.normalize_out(exp):
                row.set_result("PASS", out.rstrip("\n"), ms)
            else:
                row.set_result("WA", out.rstrip("\n"), ms)

    def run_all_testcases(self):
        if not self.rows:
            return
        run_cmd, err = self.compile_source()
        passed = 0
        total = len(self.rows)
        for row in self.rows:
            if err:
                row.set_result("CE", err, 0)
                continue
            inp = row.tc_data.get("input", "")
            exp = row.tc_data.get("output", "")
            st, out, ms = self.execute_tc(run_cmd, inp)
            if st == "OK" and self.normalize_out(out) == self.normalize_out(exp):
                row.set_result("PASS", out.rstrip("\n"), ms)
                passed += 1
            elif st == "OK":
                row.set_result("WA", out.rstrip("\n"), ms)
            else:
                row.set_result(st, out, ms)
        self.pass_badge.set_text(f"{passed} / {total} passed")
        ctx = self.pass_badge.get_style_context()
        ctx.remove_class("badge-neutral")
        ctx.remove_class("badge-pass")
        ctx.remove_class("badge-fail")
        ctx.add_class("badge-pass" if passed == total and total > 0 else "badge-fail")

    def poll_active_file(self):
        if os.path.isfile("/tmp/.cb_active_file"):
            try:
                with open("/tmp/.cb_active_file", "r") as f:
                    p = f.read().strip()
                if p and p != self.src_path and os.path.isfile(p) and p.endswith((".c", ".cpp", ".cc", ".cxx", ".py")):
                    self.load_file(p)
            except Exception:
                pass
        if os.path.isfile("/tmp/.cb_cph_run_trigger"):
            try:
                mt = os.path.getmtime("/tmp/.cb_cph_run_trigger")
                if mt > self.last_trigger_mtime:
                    self.last_trigger_mtime = mt
                    self.present()
                    self.run_all_testcases()
            except Exception:
                pass
        return True

if __name__ == "__main__":
    # Touch trigger file so any already-running instance runs testcases & focuses itself
    if "--run" in sys.argv:
        try:
            with open("/tmp/.cb_cph_run_trigger", "w") as tf:
                tf.write(str(time.time()))
        except Exception:
            pass
    pid_file = "/tmp/.cb_cph_judge.pid"
    if os.path.isfile(pid_file):
        try:
            with open(pid_file, "r") as pf:
                old_pid = int(pf.read().strip())
            os.kill(old_pid, 0)
            # Already running! It will pick up /tmp/.cb_cph_run_trigger within 350ms
            sys.exit(0)
        except Exception:
            pass
    try:
        with open(pid_file, "w") as pf:
            pf.write(str(os.getpid()))
    except Exception:
        pass
    win = CPHJudgeWindow()
    win.connect("destroy", Gtk.main_quit)
    win.show_all()
    for r in win.rows:
        r.out_box.hide()
    if "--run" in sys.argv:
        GLib.idle_add(win.run_all_testcases)
    Gtk.main()
