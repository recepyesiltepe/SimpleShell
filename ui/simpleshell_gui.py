#!/usr/bin/env python3
import json
import os
import re
import select
import signal
import subprocess
import threading
import tkinter as tk
from tkinter import messagebox
from tkinter.scrolledtext import ScrolledText


class SimpleShellGui:
    def __init__(self, root: tk.Tk) -> None:
        self.root = root
        self.settings_path = os.path.expanduser("~/.simpleshell_ui.json")
        self.settings = self.load_settings()

        self.font_size = int(self.settings.get("font_size", 11))
        self.theme = self.settings.get("theme", "dark")
        geometry = self.settings.get("geometry", "900x600")

        self.root.title("SimpleShell UI")
        self.root.geometry(geometry)

        self.output = ScrolledText(root, wrap=tk.WORD, font=("monospace", self.font_size), insertwidth=2)
        self.output.pack(fill=tk.BOTH, expand=True, padx=8, pady=(8, 4))

        controls = tk.Frame(root)
        controls.pack(fill=tk.X, padx=8, pady=(0, 8))
        input_hint = tk.Label(controls, text="Click console and type. Press Enter to run.")
        input_hint.pack(side=tk.LEFT, fill=tk.X, expand=True)

        clear_button = tk.Button(controls, text="Clear", width=10, command=self.clear_output)
        clear_button.pack(side=tk.LEFT, padx=(6, 0))
        self.font_var = tk.IntVar(value=self.font_size)
        self.theme_var = tk.StringVar(value=self.theme)

        self.process = None
        self.reader_thread = None
        self.stop_reader = threading.Event()
        self.current_input = ""

        self.apply_theme()
        self.bind_shortcuts()
        self.bind_terminal_input()
        self.root.bind_all("<Button-1>", self.on_global_left_click, add="+")
        self.root.bind("<Escape>", self.on_escape_press)
        self.root.protocol("WM_DELETE_WINDOW", self.on_close)
        self.start_shell()
        self.output.focus_set()

    def load_settings(self) -> dict:
        try:
            with open(self.settings_path, "r", encoding="utf-8") as file:
                data = json.load(file)
                if isinstance(data, dict):
                    return data
        except (FileNotFoundError, json.JSONDecodeError, OSError):
            pass
        return {}

    def save_settings(self) -> None:
        settings = {
            "geometry": self.root.geometry(),
            "font_size": self.font_var.get(),
            "theme": self.theme_var.get(),
        }
        try:
            with open(self.settings_path, "w", encoding="utf-8") as file:
                json.dump(settings, file, indent=2)
        except OSError:
            pass

    def apply_theme(self) -> None:
        if self.theme_var.get() == "light":
            output_bg = "#ffffff"
            output_fg = "#111111"
            insert_color = "#111111"
            red = "#c62828"
            green = "#2e7d32"
        else:
            output_bg = "#1e1e1e"
            output_fg = "#d4d4d4"
            insert_color = "#d4d4d4"
            red = "#ef5350"
            green = "#66bb6a"
        self.output.configure(background=output_bg, foreground=output_fg, insertbackground=insert_color)
        self.output.tag_configure("ansi_default", foreground=output_fg)
        self.output.tag_configure("ansi_red", foreground=red)
        self.output.tag_configure("ansi_green", foreground=green)

    def on_theme_change(self, _event=None) -> None:
        self.apply_theme()

    def bind_shortcuts(self) -> None:
        self.root.bind("<Control-l>", self.on_shortcut_clear_output)
        self.root.bind("<Control-L>", self.on_shortcut_clear_output)
        self.root.bind("<Control-r>", self.on_shortcut_focus_console)
        self.root.bind("<Control-R>", self.on_shortcut_focus_console)
        self.root.bind("<Control-plus>", self.on_shortcut_font_increase)
        self.root.bind("<Control-KP_Add>", self.on_shortcut_font_increase)
        self.root.bind("<Control-minus>", self.on_shortcut_font_decrease)
        self.root.bind("<Control-KP_Subtract>", self.on_shortcut_font_decrease)

    def bind_terminal_input(self) -> None:
        self.output.bind("<Button-1>", self.on_console_click)
        self.output.bind("<Button-3>", self.on_console_right_click)
        self.output.bind("<KeyPress>", self.on_console_keypress)
        self.output.bind("<<Paste>>", lambda _e: "break")

    def build_context_menu(self) -> None:
        self.context_menu = tk.Menu(self.root, tearoff=0)
        self.context_menu.add_command(label="Clear", command=self.clear_output)

        theme_menu = tk.Menu(self.context_menu, tearoff=0)
        theme_menu.add_radiobutton(label="Dark", variable=self.theme_var, value="dark",
                                   command=self.on_theme_change)
        theme_menu.add_radiobutton(label="Light", variable=self.theme_var, value="light",
                                   command=self.on_theme_change)
        self.context_menu.add_cascade(label="Theme", menu=theme_menu)

        font_menu = tk.Menu(self.context_menu, tearoff=0)
        for size in [10, 11, 12, 14, 16, 18]:
            font_menu.add_radiobutton(
                label=f"{size}",
                variable=self.font_var,
                value=size,
                command=self.on_font_change,
            )
        self.context_menu.add_cascade(label="Font Size", menu=font_menu)

    def dismiss_context_menu(self) -> None:
        if not hasattr(self, "context_menu"):
            return
        try:
            self.context_menu.unpost()
            self.context_menu.grab_release()
        except tk.TclError:
            pass

    def _set_insert_to_end(self) -> None:
        self.output.mark_set(tk.INSERT, tk.END)
        self.output.see(tk.END)

    def _render_current_input(self, previous_input: str) -> None:
        if previous_input:
            self.output.delete(f"end-{len(previous_input)+1}c", "end-1c")
        if self.current_input:
            self.output.insert("end-1c", self.current_input, ("ansi_default",))
        self._set_insert_to_end()

    def on_font_change(self, _event=None) -> None:
        try:
            size = int(self.font_var.get())
        except (ValueError, tk.TclError):
            return
        if size < 8 or size > 28:
            return
        self.output.configure(font=("monospace", size))

    def set_font_size(self, size: int) -> None:
        if size < 8:
            size = 8
        if size > 28:
            size = 28
        self.font_var.set(size)
        self.on_font_change()

    def on_shortcut_clear_output(self, _event=None) -> str:
        self.clear_output()
        return "break"

    def on_shortcut_focus_console(self, _event=None) -> str:
        self.output.focus_set()
        self._set_insert_to_end()
        return "break"

    def on_shortcut_font_increase(self, _event=None) -> str:
        self.set_font_size(self.font_var.get() + 1)
        return "break"

    def on_shortcut_font_decrease(self, _event=None) -> str:
        self.set_font_size(self.font_var.get() - 1)
        return "break"

    def on_console_click(self, _event=None) -> str:
        self.dismiss_context_menu()
        self.output.focus_set()
        self._set_insert_to_end()
        return "break"

    def on_console_right_click(self, event: tk.Event) -> str:
        self.output.focus_set()
        self._set_insert_to_end()
        try:
            self.context_menu.tk_popup(event.x_root, event.y_root)
        finally:
            self.context_menu.grab_release()
        return "break"

    def on_global_left_click(self, event: tk.Event) -> None:
        widget = event.widget
        if isinstance(widget, tk.Menu):
            return
        self.dismiss_context_menu()

    def on_escape_press(self, _event=None) -> str:
        self.dismiss_context_menu()
        return "break"

    def on_console_keypress(self, event: tk.Event) -> str:
        if not self.process or not self.process.stdin or self.process.poll() is not None:
            return "break"

        if event.keysym == "BackSpace":
            if not self.current_input:
                return "break"
            prev_line = self.current_input
            self.current_input = prev_line[:-1]
            self._render_current_input(prev_line)
            return "break"

        if event.keysym == "Return":
            command = self.current_input
            self.current_input = ""
            self.output.insert("end-1c", "\n", ("ansi_default",))
            self._set_insert_to_end()
            try:
                self.process.stdin.write((command + "\n").encode("utf-8"))
                self.process.stdin.flush()
            except OSError:
                self.append_output("\n[Unable to write to shell process]\n")
            return "break"

        if event.keysym == "Tab":
            prev_line = self.current_input
            self.current_input = prev_line + "\t"
            self._render_current_input(prev_line)
            return "break"

        if event.char and event.char.isprintable():
            prev_line = self.current_input
            self.current_input = prev_line + event.char
            self._render_current_input(prev_line)
        return "break"

    def append_output(self, text: str) -> None:
        ansi_pattern = re.compile(r"\x1b\[([0-9;]*)m")
        typed_restore = self.current_input
        if typed_restore:
            self.output.delete(f"end-{len(typed_restore)+1}c", "end-1c")
        current_tag = "ansi_default"
        text = text.replace("\r", "")
        cursor = 0
        for match in ansi_pattern.finditer(text):
            start, end = match.span()
            if start > cursor:
                self.output.insert(tk.END, text[cursor:start], (current_tag,))

            code_text = match.group(1)
            if code_text == "":
                current_tag = "ansi_default"
            else:
                codes = code_text.split(";")
                for code in codes:
                    if code == "0" or code == "39":
                        current_tag = "ansi_default"
                    elif code == "31":
                        current_tag = "ansi_red"
                    elif code == "32":
                        current_tag = "ansi_green"
            cursor = end

        if cursor < len(text):
            self.output.insert("end-1c", text[cursor:], (current_tag,))
        if typed_restore:
            self.output.insert("end-1c", typed_restore, ("ansi_default",))
        self._set_insert_to_end()

    def append_output_threadsafe(self, text: str) -> None:
        self.root.after(0, lambda: self.append_output(text))

    def clear_output(self) -> None:
        self.output.delete("1.0", tk.END)
        self.output.insert("1.0", "")
        self.current_input = ""
        self._set_insert_to_end()

    def shell_reader_loop(self) -> None:
        while not self.stop_reader.is_set():
            if not self.process or not self.process.stdout:
                break
            try:
                stdout_fd = self.process.stdout.fileno()
                ready, _, _ = select.select([stdout_fd], [], [], 0.2)
                if not ready:
                    continue
                data = os.read(stdout_fd, 4096)
                if not data:
                    break
                text = data.decode("utf-8", errors="replace")
                self.append_output_threadsafe(text)
            except OSError:
                break

        if self.process and self.process.poll() is not None:
            code = self.process.returncode
            self.append_output_threadsafe(f"\n[SimpleShell exited with code {code}]\n")

    def start_shell(self) -> None:
        shell_binary = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "bin", "SimpleShell"))
        if not os.path.exists(shell_binary):
            messagebox.showerror("SimpleShell UI", f"Binary not found: {shell_binary}\nRun `make` first.")
            return

        self.process = subprocess.Popen(
            [shell_binary],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            close_fds=True,
            start_new_session=True,
            env={**os.environ, "SIMPLESHELL_NO_TTY_EDITOR": "1"},
        )

        self.stop_reader.clear()
        self.current_input = ""
        self.reader_thread = threading.Thread(target=self.shell_reader_loop, daemon=True)
        self.reader_thread.start()
        self.append_output("[SimpleShell started]\n")
        self._set_insert_to_end()

    def stop_shell(self) -> None:
        self.stop_reader.set()
        if self.process and self.process.stdin:
            try:
                self.process.stdin.close()
            except OSError:
                pass
        if self.process and self.process.poll() is None:
            process_group_id = None
            try:
                process_group_id = os.getpgid(self.process.pid)
            except ProcessLookupError:
                process_group_id = None

            try:
                if process_group_id is not None:
                    os.killpg(process_group_id, signal.SIGTERM)
                else:
                    self.process.terminate()
            except ProcessLookupError:
                pass
            else:
                try:
                    self.process.wait(timeout=0.15)
                except subprocess.TimeoutExpired:
                    try:
                        if process_group_id is not None:
                            os.killpg(process_group_id, signal.SIGKILL)
                        else:
                            self.process.kill()
                    except ProcessLookupError:
                        pass
                    try:
                        self.process.wait(timeout=0.15)
                    except subprocess.TimeoutExpired:
                        pass
        if self.process and self.process.stdout:
            try:
                self.process.stdout.close()
            except OSError:
                pass
        if self.reader_thread and self.reader_thread.is_alive():
            self.reader_thread.join(timeout=0.2)
        self.process = None
        self.reader_thread = None

    def restart_shell(self) -> None:
        self.stop_shell()
        self.start_shell()

    def on_close(self) -> None:
        self.save_settings()
        self.dismiss_context_menu()
        self.stop_shell()
        try:
            self.root.quit()
            self.root.destroy()
        finally:
            os._exit(0)


def main() -> None:
    root = tk.Tk()
    app = SimpleShellGui(root)
    app.build_context_menu()
    _ = app
    root.mainloop()


if __name__ == "__main__":
    main()
