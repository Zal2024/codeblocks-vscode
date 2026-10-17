import hashlib
import json
import os
import re
import subprocess
import sys
import threading
import time
import tkinter as tk
from tkinter import ttk, messagebox, scrolledtext

def get_prob_path(src_path):
    d = os.path.dirname(src_path)
    base = os.path.basename(src_path)
    cph_dir = os.path.join(d, ".cph")
    os.makedirs(cph_dir, exist_ok=True)
    h = hashlib.md5(src_path.encode("utf-8")).hexdigest()
    return os.path.join(cph_dir, f".{base}_{h}.prob")

def detect_initial_file():
    for arg in sys.argv[1:]:
        if not arg.startswith("-") and os.path.isfile(arg):
            return os.path.abspath(arg)
    
    # Try %TEMP%\cb_active_file.txt
    temp_p = os.path.join(os.environ.get("TEMP", "C:\\Temp"), "cb_active_file.txt")
    if os.path.isfile(temp_p):
        try:
            with open(temp_p, "r", encoding="utf-8") as f:
                p = f.read().strip()
            if os.path.isfile(p):
                return p
        except Exception:
            pass

    # Fallback to recent in Documents
    docs = os.path.expanduser("~/Documents")
    best_p, best_t = None, -1
    if os.path.isdir(docs):
        for root, _, files in os.walk(docs):
            if "codeblocks-vscode" in root or ".cph" in root:
                continue
            for fn in files:
                if fn.endswith((".c", ".cpp", ".cc")):
                    fp = os.path.join(root, fn)
                    try:
                        t = os.path.getmtime(fp)
                        if t > best_t:
                            best_t, best_p = t, fp
                    except Exception:
                        pass
    return best_p

def diff_output(expected, received):
    def norm(s):
        lines = [l.rstrip() for l in s.replace("\r\n", "\n").rstrip().split("\n")] if s.strip() else []
        return lines
    exp_l = norm(expected)
    rec_l = norm(received)
    is_match = (exp_l == rec_l)
    return is_match

class CPHJudgeWindow:
    def __init__(self, root):
        self.root = root
        self.root.title("CPH Judge: Results (Windows Edition)")
        self.root.geometry("460x720")
        self.root.configure(bg="#1E1E1E")
        self.root.attributes("-topmost", True)

        self.src_path = detect_initial_file()
        self.prob = None
        self.test_frames = []

        self.setup_ui()
        self.load_current_problem()

        # Start background polling for active file changes or run triggers
        self.poll_active_file()

        if "--run" in sys.argv:
            self.root.after(400, self.run_all_testcases)

    def setup_ui(self):
        # Header bar
        header = tk.Frame(self.root, bg="#252526", height=42)
        header.pack(fill="x", side="top")

        self.lbl_title = tk.Label(header, text="CPH Judge", fg="#CCCCCC", bg="#252526", font=("Segoe UI", 11, "bold"))
        self.lbl_title.pack(side="left", padx=10, pady=8)

        btn_run_all = tk.Button(header, text="▶ Run All", bg="#0E639C", fg="white", activebackground="#1177BB",
                                activeforeground="white", relief="flat", font=("Segoe UI", 9, "bold"),
                                command=self.run_all_testcases)
        btn_run_all.pack(side="right", padx=10, pady=6)

        btn_new_tc = tk.Button(header, text="+ New", bg="#3C3C3C", fg="#CCCCCC", relief="flat",
                               font=("Segoe UI", 9), command=self.add_new_testcase)
        btn_new_tc.pack(side="right", padx=4, pady=6)

        # File status bar
        self.status_bar = tk.Label(self.root, text="", fg="#858585", bg="#1E1E1E", font=("Segoe UI", 8), anchor="w")
        self.status_bar.pack(fill="x", padx=10, pady=(4, 0))

        # Main scrollable canvas
        container = tk.Frame(self.root, bg="#1E1E1E")
        container.pack(fill="both", expand=True, padx=6, pady=6)

        self.canvas = tk.Canvas(container, bg="#1E1E1E", highlightthickness=0)
        scrollbar = ttk.Scrollbar(container, orient="vertical", command=self.canvas.yview)
        self.scrollable_frame = tk.Frame(self.canvas, bg="#1E1E1E")

        self.scrollable_frame.bind(
            "<Configure>",
            lambda e: self.canvas.configure(scrollregion=self.canvas.bbox("all"))
        )
        self.canvas.create_window((0, 0), window=self.scrollable_frame, anchor="nw", width=425)
        self.canvas.configure(yscrollcommand=scrollbar.set)

        self.canvas.pack(side="left", fill="both", expand=True)
        scrollbar.pack(side="right", fill="y")

    def load_current_problem(self):
        for widget in self.scrollable_frame.winfo_children():
            widget.destroy()
        self.test_frames.clear()

        if not self.src_path or not os.path.isfile(self.src_path):
            self.lbl_title.config(text="CPH Judge: No Active File")
            self.status_bar.config(text="Open a C/C++ file in Code::Blocks to start.")
            return

        stem = os.path.splitext(os.path.basename(self.src_path))[0]
        self.lbl_title.config(text=f"CPH: {os.path.basename(self.src_path)}")
        self.status_bar.config(text=self.src_path)

        pfile = get_prob_path(self.src_path)
        if os.path.isfile(pfile):
            try:
                with open(pfile, "r", encoding="utf-8") as f:
                    self.prob = json.load(f)
            except Exception:
                self.prob = None

        if not self.prob:
            self.prob = {
                "name": f"Local: {stem}",
                "url": self.src_path,
                "tests": [{"id": int(time.time() * 1000), "input": "", "output": ""}],
                "interactive": False,
                "memoryLimit": 1024,
                "timeLimit": 3000,
                "srcPath": self.src_path,
                "group": "local",
                "local": True
            }
            self.save_problem()

        for idx, tc in enumerate(self.prob.get("tests", [])):
            self.render_testcase_card(idx, tc)

    def save_problem(self):
        if not self.src_path or not self.prob:
            return
        pfile = get_prob_path(self.src_path)
        try:
            with open(pfile, "w", encoding="utf-8") as f:
                json.dump(self.prob, f, indent=2)
        except Exception:
            pass

    def render_testcase_card(self, idx, tc):
        card = tk.Frame(self.scrollable_frame, bg="#252526", bd=1, relief="solid")
        card.pack(fill="x", pady=5, padx=2)

        # Card header
        hdr = tk.Frame(card, bg="#2D2D2D", height=28)
        hdr.pack(fill="x", side="top")

        lbl_num = tk.Label(hdr, text=f"Testcase #{idx + 1}", fg="#CCCCCC", bg="#2D2D2D", font=("Segoe UI", 9, "bold"))
        lbl_num.pack(side="left", padx=8, pady=4)

        lbl_res = tk.Label(hdr, text="", fg="#CCCCCC", bg="#2D2D2D", font=("Segoe UI", 8, "bold"))
        lbl_res.pack(side="left", padx=4)

        btn_del = tk.Button(hdr, text="🗑", fg="#F48771", bg="#2D2D2D", relief="flat",
                            command=lambda t=tc: self.delete_testcase(t))
        btn_del.pack(side="right", padx=6)

        btn_run = tk.Button(hdr, text="▷ Run", fg="#4EC9B0", bg="#2D2D2D", relief="flat",
                            font=("Segoe UI", 8, "bold"),
                            command=lambda t=tc, r_lbl=lbl_res: self.run_single_testcase(t, r_lbl))
        btn_run.pack(side="right", padx=4)

        # Input & Output boxes
        body = tk.Frame(card, bg="#252526")
        body.pack(fill="both", expand=True, padx=8, pady=6)

        tk.Label(body, text="Input:", fg="#858585", bg="#252526", font=("Segoe UI", 8)).pack(anchor="w")
        txt_in = tk.Text(body, height=3, bg="#1E1E1E", fg="#D4D4D4", insertbackground="#CCCCCC",
                         relief="flat", font=("Consolas", 9))
        txt_in.insert("1.0", tc.get("input", ""))
        txt_in.pack(fill="x", pady=(0, 4))

        tk.Label(body, text="Expected Output:", fg="#858585", bg="#252526", font=("Segoe UI", 8)).pack(anchor="w")
        txt_out = tk.Text(body, height=3, bg="#1E1E1E", fg="#D4D4D4", insertbackground="#CCCCCC",
                          relief="flat", font=("Consolas", 9))
        txt_out.insert("1.0", tc.get("output", ""))
        txt_out.pack(fill="x", pady=(0, 4))

        def update_tc(event=None):
            tc["input"] = txt_in.get("1.0", "end-1c")
            tc["output"] = txt_out.get("1.0", "end-1c")
            self.save_problem()

        txt_in.bind("<KeyRelease>", update_tc)
        txt_out.bind("<KeyRelease>", update_tc)

        tc["_res_lbl"] = lbl_res
        self.test_frames.append(card)

    def add_new_testcase(self):
        if not self.prob:
            return
        new_tc = {"id": int(time.time() * 1000), "input": "", "output": ""}
        self.prob.setdefault("tests", []).append(new_tc)
        self.save_problem()
        self.render_testcase_card(len(self.prob["tests"]) - 1, new_tc)

    def delete_testcase(self, tc):
        if not self.prob or "tests" not in self.prob:
            return
        self.prob["tests"] = [t for t in self.prob["tests"] if t["id"] != tc["id"]]
        self.save_problem()
        self.load_current_problem()

    def compile_current_file(self):
        if not self.src_path or not os.path.isfile(self.src_path):
            return None
        ext = os.path.splitext(self.src_path)[1].lower()
        cc = "g++" if ext in (".cpp", ".cc", ".cxx") else "gcc"
        out_bin = os.path.join(os.environ.get("TEMP", "C:\\Temp"), f"cph_bin_{os.getpid()}.exe")
        cmd = [cc, "-O2", "-Wall", self.src_path, "-o", out_bin]
        res = subprocess.run(cmd, capture_output=True, text=True)
        if res.returncode != 0:
            messagebox.showerror("Compilation Error", res.stderr)
            return None
        return out_bin

    def run_single_testcase(self, tc, res_lbl):
        def worker():
            res_lbl.config(text="Compiling...", fg="#CCA700")
            out_bin = self.compile_current_file()
            if not out_bin:
                res_lbl.config(text="Build Error", fg="#F44747")
                return

            res_lbl.config(text="Running...", fg="#3794FF")
            inp = tc.get("input", "")
            exp = tc.get("output", "")
            t0 = time.perf_counter()
            try:
                proc = subprocess.Popen(
                    [out_bin],
                    stdin=subprocess.PIPE,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    text=True
                )
                stdout, stderr = proc.communicate(input=inp, timeout=3.0)
                ms = max(1, int((time.perf_counter() - t0) * 1000))
                matched = diff_output(exp, stdout)
                if matched:
                    res_lbl.config(text=f"✓ Passed ({ms}ms)", fg="#4EC9B0")
                else:
                    res_lbl.config(text=f"✗ Failed ({ms}ms)", fg="#F44747")
            except subprocess.TimeoutExpired:
                proc.kill()
                res_lbl.config(text="Timed Out (>3s)", fg="#F44747")
            except Exception as e:
                res_lbl.config(text="Runtime Error", fg="#F44747")

        threading.Thread(target=worker, daemon=True).start()

    def run_all_testcases(self):
        if not self.prob:
            return
        out_bin = self.compile_current_file()
        if not out_bin:
            return

        def worker():
            for tc in self.prob.get("tests", []):
                res_lbl = tc.get("_res_lbl")
                if not res_lbl:
                    continue
                inp = tc.get("input", "")
                exp = tc.get("output", "")
                t0 = time.perf_counter()
                try:
                    proc = subprocess.Popen(
                        [out_bin],
                        stdin=subprocess.PIPE,
                        stdout=subprocess.PIPE,
                        stderr=subprocess.PIPE,
                        text=True
                    )
                    stdout, stderr = proc.communicate(input=inp, timeout=3.0)
                    ms = max(1, int((time.perf_counter() - t0) * 1000))
                    matched = diff_output(exp, stdout)
                    if matched:
                        res_lbl.config(text=f"✓ Passed ({ms}ms)", fg="#4EC9B0")
                    else:
                        res_lbl.config(text=f"✗ Failed ({ms}ms)", fg="#F44747")
                except subprocess.TimeoutExpired:
                    proc.kill()
                    res_lbl.config(text="Timed Out (>3s)", fg="#F44747")
                except Exception:
                    res_lbl.config(text="Runtime Error", fg="#F44747")

        threading.Thread(target=worker, daemon=True).start()

    def poll_active_file(self):
        temp_p = os.path.join(os.environ.get("TEMP", "C:\\Temp"), "cb_active_file.txt")
        if os.path.isfile(temp_p):
            try:
                with open(temp_p, "r", encoding="utf-8") as f:
                    p = f.read().strip()
                if p and p != self.src_path and os.path.isfile(p):
                    self.src_path = p
                    self.load_current_problem()
            except Exception:
                pass
        self.root.after(500, self.poll_active_file)

if __name__ == "__main__":
    root = tk.Tk()
    app = CPHJudgeWindow(root)
    root.mainloop()
