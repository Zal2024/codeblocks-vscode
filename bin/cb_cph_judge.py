#!/usr/bin/env python3
import gi
gi.require_version('Gtk', '3.0')
gi.require_version('WebKit2', '4.1')
from gi.repository import Gtk, Gdk, GLib, WebKit2
import hashlib
import http.server
import json
import os
import re
import socketserver
import subprocess
import sys
import threading
import time
import webbrowser

CPH_DIST = os.path.expanduser("~/.local/share/codeblocks/cph_dist")
if not os.path.isdir(CPH_DIST):
    CPH_DIST = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "cph_dist")

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
    if os.path.isfile("/tmp/.cb_active_file"):
        try:
            with open("/tmp/.cb_active_file", "r") as f:
                p = f.read().strip()
            if os.path.isfile(p):
                return p
        except Exception:
            pass
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

def diff_output(expected, received):
    def norm_lines(s):
        return [l.rstrip() for l in s.replace("\r\n", "\n").rstrip().split("\n")] if s.strip() else []
    def norm_tokens(s):
        parts = re.split(r"(\n|[ \t]+)", s.replace("\r\n", "\n").rstrip())
        return [p for p in parts if p]

    exp_l = norm_lines(expected)
    rec_l = norm_lines(received)
    max_len = max(len(exp_l), len(rec_l))
    lines = []
    for i in range(max_len):
        e = exp_l[i] if i < len(exp_l) else None
        r = rec_l[i] if i < len(rec_l) else None
        t = "extra" if e is None else ("missing" if r is None else ("match" if e == r else "changed"))
        lines.append({"lineNumber": i + 1, "expected": e, "received": r, "type": t})

    is_match = all(x["type"] == "match" for x in lines)
    diff_cnt = sum(1 for x in lines if x["type"] != "match")
    summary = "All lines match." if is_match else f"{diff_cnt} line{'' if diff_cnt == 1 else 's'} differ{'s' if diff_cnt == 1 else ''}."

    et = norm_tokens(expected)
    rt = norm_tokens(received)
    n, m = len(et), len(rt)
    token_diff = []
    if n * m <= 250000:
        dp = [[0] * (m + 1) for _ in range(n + 1)]
        for i in range(1, n + 1):
            for j in range(1, m + 1):
                if et[i - 1] == rt[j - 1]:
                    dp[i][j] = dp[i - 1][j - 1] + 1
                else:
                    dp[i][j] = max(dp[i - 1][j], dp[i][j - 1])
        a, b = n, m
        while a > 0 or b > 0:
            if a > 0 and b > 0 and et[a - 1] == rt[b - 1]:
                token_diff.insert(0, {"token": rt[b - 1], "status": "match"})
                a -= 1
                b -= 1
            elif b > 0 and (a == 0 or dp[a][b - 1] >= dp[a - 1][b]):
                token_diff.insert(0, {"token": rt[b - 1], "status": "extra"})
                b -= 1
            else:
                token_diff.insert(0, {"token": et[a - 1], "status": "missing"})
                a -= 1
    return {"isMatch": is_match, "lines": lines, "summary": summary, "tokenDiff": token_diff}


class CPHAppWindow(Gtk.Window):
    def __init__(self):
        super().__init__(title="CPH Judge: Results")
        self.set_default_size(375, 720)
        self.set_keep_above(True)

        screen = Gdk.Screen.get_default()
        if screen:
            self.move(max(0, screen.get_width() - 390), 40)

        self.src_path = detect_initial_file()
        self.online_judge_env = False
        self.running_procs = []
        self.last_trigger_mtime = 0
        self.webview_ready = False

        # Start local HTTP server to serve original CPH dist/ and receive acquireVsCodeApi().postMessage
        self.port = self.start_http_server()

        self.webview = WebKit2.WebView()
        settings = self.webview.get_settings()
        settings.set_enable_javascript(True)
        settings.set_enable_webaudio(True)
        settings.set_enable_media(True)
        self.webview.set_background_color(Gdk.RGBA(0.118, 0.118, 0.118, 1.0))

        self.add(self.webview)
        self.webview.load_uri(f"http://127.0.0.1:{self.port}/__cph_app.html")

        GLib.timeout_add(350, self.poll_active_file)

    def start_http_server(self):
        app = self
        with open(os.path.join(CPH_DIST, "translations_en.json"), "r", encoding="utf-8") as f:
            translations = json.load(f)

        class Handler(http.server.SimpleHTTPRequestHandler):
            def __init__(self, *args, **kwargs):
                super().__init__(*args, directory=CPH_DIST, **kwargs)

            def log_message(self, format, *args):
                pass

            def do_GET(self):
                if self.path.startswith("/__cph_app.html"):
                    base_url = f"http://127.0.0.1:{app.port}"
                    init_globals = {
                        "meowAudioUri": f"{base_url}/meow.mp3",
                        "remoteMessage": " ",
                        "generatedJsonUri": f"{base_url}/static/generated.json",
                        "remoteServerAddress": "https://cph-Telemetry.divyanshuagrawal.com",
                        "showLiveUserCount": False,
                        "showOutputDifference": True,
                        "translations": translations,
                        "pythonCommand": "python3",
                        "cphSubmitServerFailed": False,
                        "cphSubmitServerPort": 27121,
                    }
                    html = f"""<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8" />
    <link rel="stylesheet" href="{base_url}/app.css" />
    <link rel="stylesheet" href="{base_url}/codicon.css" />
    <style>
        :root {{
            --vscode-font-family: "Segoe UI", "Ubuntu", sans-serif;
            --vscode-editor-font-family: "JetBrains Mono", monospace;
            --vscode-foreground: #CCCCCC;
            --vscode-panel-background: #1E1E1E;
            --vscode-editor-background: #1E1E1E;
            --vscode-input-background: #181818;
            --vscode-input-foreground: #D4D4D4;
            --vscode-focusBorder: #007FD4;
            --vscode-editorError-foreground: #F44747;
            --vscode-testing-iconFailed: #F44747;
            --vscode-notebookStatusSuccessIcon-foreground: #4EC9B0;
            --vscode-menu-background: #252526;
            --vscode-menu-foreground: #CCCCCC;
            --vscode-menu-border: #3C3C3C;
            --vscode-notifications-background: #252526;
            --vscode-notifications-foreground: #CCCCCC;
            --vscode-notifications-border: #3C3C3C;
            --vscode-textLink-foreground: #3794FF;
            --vscode-textLink-activeForeground: #4DACFF;
            --vscode-diffEditor-insertedTextBackground: rgba(155, 185, 85, 0.25);
            --vscode-diffEditor-removedTextBackground: rgba(255, 0, 0, 0.25);
        }}
        body {{
            background-color: #1E1E1E;
            color: #CCCCCC;
            margin: 0;
            padding: 6px;
        }}
    </style>
</head>
<body>
    <div id="app">Loading CPH Judge...</div>
    <script>
        window.acquireVsCodeApi = function() {{
            let state = {{}};
            return {{
                postMessage: function(msg) {{
                    fetch("/__cph_msg", {{
                        method: "POST",
                        headers: {{ "Content-Type": "application/json" }},
                        body: JSON.stringify(msg)
                    }});
                }},
                getState: function() {{ return state; }},
                setState: function(s) {{ state = s; return state; }}
            }};
        }};
        window.vscodeApi = acquireVsCodeApi();
        Object.assign(window, {json.dumps(init_globals)});
        document.addEventListener('DOMContentLoaded', () => {{
            vscodeApi.postMessage({{ command: 'get-initial-problem' }});
            vscodeApi.postMessage({{ command: 'online-judge-env', value: 'default' }});
        }});
    </script>
    <script src="{base_url}/frontend.module.js"></script>
</body>
</html>"""
                    data = html.encode("utf-8")
                    self.send_response(200)
                    self.send_header("Content-Type", "text/html; charset=utf-8")
                    self.send_header("Content-Length", str(len(data)))
                    self.end_headers()
                    self.wfile.write(data)
                    return
                return super().do_GET()

            def do_POST(self):
                if self.path == "/__cph_msg":
                    length = int(self.headers.get("Content-Length", "0"))
                    raw = self.rfile.read(length).decode("utf-8")
                    self.send_response(200)
                    self.end_headers()
                    self.wfile.write(b"OK")
                    try:
                        msg = json.loads(raw)
                        GLib.idle_add(app.handle_webview_message, msg)
                    except Exception:
                        pass
                    return
                self.send_response(404)
                self.end_headers()

        httpd = socketserver.ThreadingTCPServer(("127.0.0.1", 0), Handler)
        httpd.daemon_threads = True
        port = httpd.server_address[1]
        t = threading.Thread(target=httpd.serve_forever, daemon=True)
        t.start()
        return port

    def send_to_webview(self, msg):
        js = f"window.postMessage({json.dumps(msg)}, '*');"
        self.webview.run_javascript(js, None, None, None)

    def get_problem_for_path(self, path, auto_create=False):
        if not path or not os.path.isfile(path):
            return None
        pfile = get_prob_path(path)
        if os.path.isfile(pfile):
            try:
                with open(pfile, "r", encoding="utf-8") as f:
                    prob = json.load(f)
                prob["srcPath"] = path
                return prob
            except Exception:
                pass
        if auto_create:
            stem = os.path.splitext(os.path.basename(path))[0]
            prob = {
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
            self.save_problem(path, prob)
            return prob
        return None

    def save_problem(self, src_path, prob):
        if not src_path:
            return
        pfile = get_prob_path(src_path)
        try:
            with open(pfile, "w", encoding="utf-8") as f:
                json.dump(prob, f)
        except Exception:
            pass

    def handle_webview_message(self, msg):
        cmd = msg.get("command")
        if cmd == "get-initial-problem":
            self.webview_ready = True
            prob = self.get_problem_for_path(self.src_path, auto_create=True)
            self.send_to_webview({
                "command": "new-problem",
                "problem": prob,
                "onlineJudgeEnv": self.online_judge_env,
            })
            if "--run" in sys.argv and prob:
                sys.argv.remove("--run")
                threading.Thread(target=self.run_all_and_save, args=(prob,), daemon=True).start()
        elif cmd == "online-judge-env":
            val = msg.get("value")
            if val == "true":
                self.online_judge_env = True
            elif val == "false":
                self.online_judge_env = False
            elif val == "default":
                self.send_to_webview({"command": "update-online-judge-env", "value": self.online_judge_env})
        elif cmd == "save":
            prob = msg.get("problem")
            if prob and "srcPath" in prob:
                self.save_problem(prob["srcPath"], prob)
        elif cmd == "create-local-problem":
            prob = self.get_problem_for_path(self.src_path, auto_create=True)
            self.send_to_webview({
                "command": "new-problem",
                "problem": prob,
                "onlineJudgeEnv": self.online_judge_env,
            })
        elif cmd == "delete-tcs":
            prob = msg.get("problem")
            if prob and "srcPath" in prob:
                pfile = get_prob_path(prob["srcPath"])
                if os.path.isfile(pfile):
                    os.remove(pfile)
            self.send_to_webview({
                "command": "new-problem",
                "problem": None,
                "onlineJudgeEnv": self.online_judge_env,
            })
        elif cmd == "kill-running":
            for p in list(self.running_procs):
                try:
                    p.kill()
                except Exception:
                    pass
            self.running_procs.clear()
        elif cmd == "run-single-and-save":
            prob = msg.get("problem")
            tc_id = msg.get("id")
            if prob:
                self.save_problem(prob["srcPath"], prob)
                threading.Thread(target=self.run_single_and_save, args=(prob, tc_id, False), daemon=True).start()
        elif cmd == "run-all-and-save":
            prob = msg.get("problem")
            if prob:
                self.save_problem(prob["srcPath"], prob)
                threading.Thread(target=self.run_all_and_save, args=(prob,), daemon=True).start()
        elif cmd == "url":
            url = msg.get("url")
            if url:
                webbrowser.open(url)
        elif cmd == "get-ext-logs":
            self.send_to_webview({"command": "ext-logs", "logs": "CPH Judge connected to Code::Blocks.\n"})

    def compile_file(self, src_path):
        GLib.idle_add(self.send_to_webview, {"command": "compiling-start"})
        if src_path.endswith(".py"):
            GLib.idle_add(self.send_to_webview, {"command": "compiling-stop"})
            return ["python3", src_path]
        ext = os.path.splitext(src_path)[1]
        cc = "g++" if ext in (".cpp", ".cc", ".cxx") else "gcc"
        out_bin = f"/tmp/cph_bin_{os.getuid()}"
        cmd = [cc, "-O2", "-Wall", src_path, "-o", out_bin, "-lm"]
        if self.online_judge_env:
            cmd.insert(2, "-DONLINE_JUDGE")
        res = subprocess.run(cmd, capture_output=True, text=True)
        if res.returncode != 0:
            GLib.idle_add(self.send_to_webview, {"command": "compiling-stop", "error": res.stderr})
            return None
        GLib.idle_add(self.send_to_webview, {"command": "compiling-stop"})
        return [out_bin]

    def run_single_and_save(self, prob, tc_id, skip_compile=False, run_cmd=None):
        src_path = prob["srcPath"]
        if not skip_compile:
            run_cmd = self.compile_file(src_path)
            if not run_cmd:
                return
        tc = next((t for t in prob.get("tests", []) if t["id"] == tc_id), None)
        if not tc:
            return
        GLib.idle_add(self.send_to_webview, {"command": "running", "id": tc_id, "problem": prob})

        inp = tc.get("input", "")
        exp = tc.get("output", "")
        t0 = time.perf_counter()
        timed_out = False
        stdout, stderr = "", ""
        code = 0
        sig = None

        try:
            proc = subprocess.Popen(
                run_cmd,
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                env={**os.environ, "DEBUG": "true", "CPH": "true"},
            )
            self.running_procs.append(proc)
            try:
                stdout, stderr = proc.communicate(input=inp, timeout=3.0)
                code = proc.returncode
                if code < 0:
                    sig = f"SIG{-code}"
            except subprocess.TimeoutExpired:
                timed_out = True
                proc.kill()
                stdout, stderr = proc.communicate()
                sig = "SIGTERM"
            finally:
                if proc in self.running_procs:
                    self.running_procs.remove(proc)
        except Exception as e:
            stderr = str(e)
            code = 1

        ms = max(1, int((time.perf_counter() - t0) * 1000))
        d = diff_output(exp, stdout)
        passed = (not timed_out) and (code == 0) and (sig is None) and d["isMatch"]

        result = {
            "stdout": stdout,
            "stderr": stderr,
            "code": code,
            "signal": sig,
            "time": ms,
            "timeOut": timed_out,
            "pass": passed,
            "diff": None if (timed_out or code != 0 or sig) else d,
            "id": tc_id,
        }
        GLib.idle_add(self.send_to_webview, {"command": "run-single-result", "result": result, "problem": prob})

    def run_all_and_save(self, prob):
        run_cmd = self.compile_file(prob["srcPath"])
        if not run_cmd:
            return
        for tc in prob.get("tests", []):
            self.run_single_and_save(prob, tc["id"], skip_compile=True, run_cmd=run_cmd)

    def poll_active_file(self):
        if not self.webview_ready:
            return True
        if os.path.isfile("/tmp/.cb_active_file"):
            try:
                with open("/tmp/.cb_active_file", "r") as f:
                    p = f.read().strip()
                if p and p != self.src_path and os.path.isfile(p) and p.endswith((".c", ".cpp", ".cc", ".cxx", ".py")):
                    self.src_path = p
                    prob = self.get_problem_for_path(p, auto_create=True)
                    self.send_to_webview({
                        "command": "new-problem",
                        "problem": prob,
                        "onlineJudgeEnv": self.online_judge_env,
                    })
            except Exception:
                pass
        if os.path.isfile("/tmp/.cb_cph_run_trigger"):
            try:
                mt = os.path.getmtime("/tmp/.cb_cph_run_trigger")
                if mt > self.last_trigger_mtime:
                    self.last_trigger_mtime = mt
                    self.present()
                    prob = self.get_problem_for_path(self.src_path, auto_create=True)
                    if prob:
                        self.send_to_webview({
                            "command": "new-problem",
                            "problem": prob,
                            "onlineJudgeEnv": self.online_judge_env,
                        })
                        threading.Thread(target=self.run_all_and_save, args=(prob,), daemon=True).start()
            except Exception:
                pass
        return True


if __name__ == "__main__":
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
            sys.exit(0)
        except Exception:
            pass
    try:
        with open(pid_file, "w") as pf:
            pf.write(str(os.getpid()))
    except Exception:
        pass
    win = CPHAppWindow()
    win.connect("destroy", Gtk.main_quit)
    win.show_all()
    Gtk.main()
