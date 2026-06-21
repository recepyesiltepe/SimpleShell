#!/usr/bin/env python3
import codecs
import json
import errno
import os
import pty
import select
import signal
import threading
import time
from pathlib import Path

import gi

gi.require_version("Gtk", "4.0")
gi.require_version("Gdk", "4.0")
from gi.repository import Gdk, GLib, Gtk  # noqa: E402


class SimpleShellGtkWindow(Gtk.ApplicationWindow):
    def __init__(self, app: Gtk.Application) -> None:
        super().__init__(application=app, title="SimpleShell UI")

        self.settings_path = Path.home() / ".simpleshell_ui.json"
        self.settings = self.load_settings()
        self.font_size = int(self.settings.get("font_size", 11))
        self.theme = str(self.settings.get("theme", "dark"))

        self.shell_pid: int | None = None
        self.master_fd: int | None = None
        self.exit_code: int | None = None
        self.reader_thread: threading.Thread | None = None
        self.stop_reader = threading.Event()

        self.css_provider = Gtk.CssProvider()
        self.pending_output = ""
        self.output_decoder = codecs.getincrementaldecoder("utf-8")("replace")

        self.set_default_size(
            int(self.settings.get("width", 900)),
            int(self.settings.get("height", 600)),
        )

        root = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)
        root.set_margin_top(8)
        root.set_margin_bottom(8)
        root.set_margin_start(8)
        root.set_margin_end(8)
        self.set_child(root)

        self.text_view = Gtk.TextView()
        self.text_view.set_wrap_mode(Gtk.WrapMode.WORD_CHAR)
        self.text_view.set_editable(False)
        self.text_view.set_cursor_visible(True)
        self.text_view.set_monospace(True)
        self.text_view.add_css_class("shell-console")

        self.buffer = self.text_view.get_buffer()
        self.default_tag = self.buffer.create_tag("ansi_default")
        self.red_tag = self.buffer.create_tag("ansi_red")
        self.green_tag = self.buffer.create_tag("ansi_green")
        self.current_output_tag = self.default_tag

        scroller = Gtk.ScrolledWindow()
        scroller.set_hexpand(True)
        scroller.set_vexpand(True)
        scroller.set_child(self.text_view)
        root.append(scroller)

        controls = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=6)
        root.append(controls)

        hint = Gtk.Label(label="Click console and type. Press Enter to run.")
        hint.set_hexpand(True)
        hint.set_xalign(0)
        controls.append(hint)

        clear_button = Gtk.Button(label="Clear")
        clear_button.connect("clicked", self.on_clear_clicked)
        controls.append(clear_button)

        theme_button = Gtk.Button(label="Toggle Theme")
        theme_button.connect("clicked", self.on_toggle_theme_clicked)
        controls.append(theme_button)

        font_down = Gtk.Button(label="A-")
        font_down.connect("clicked", self.on_font_decrease_clicked)
        controls.append(font_down)

        font_up = Gtk.Button(label="A+")
        font_up.connect("clicked", self.on_font_increase_clicked)
        controls.append(font_up)

        key_controller = Gtk.EventControllerKey()
        key_controller.connect("key-pressed", self.on_key_pressed)
        self.text_view.add_controller(key_controller)

        click_controller = Gtk.GestureClick()
        click_controller.connect("pressed", self.on_console_clicked)
        self.text_view.add_controller(click_controller)

        self.connect("close-request", self.on_close_request)
        self.apply_theme()
        self.start_shell()
        self.text_view.grab_focus()

    def load_settings(self) -> dict:
        try:
            with self.settings_path.open("r", encoding="utf-8") as file:
                data = json.load(file)
                if isinstance(data, dict):
                    return data
        except (FileNotFoundError, json.JSONDecodeError, OSError):
            pass
        return {}

    def save_settings(self) -> None:
        width, height = self.get_default_size()
        settings = {
            "width": width,
            "height": height,
            "font_size": self.font_size,
            "theme": self.theme,
        }
        try:
            with self.settings_path.open("w", encoding="utf-8") as file:
                json.dump(settings, file, indent=2)
        except OSError:
            pass

    def apply_theme(self) -> None:
        if self.theme == "light":
            background = "#ffffff"
            foreground = "#111111"
            red = "#c62828"
            green = "#2e7d32"
        else:
            background = "#1e1e1e"
            foreground = "#d4d4d4"
            red = "#ef5350"
            green = "#66bb6a"

        css = f"""
        textview.shell-console {{
            background: {background};
            color: {foreground};
            font-family: monospace;
            font-size: {self.font_size}pt;
        }}
        textview.shell-console text {{
            background: {background};
            color: {foreground};
        }}
        """
        self.css_provider.load_from_data(css.encode("utf-8"))
        display = Gdk.Display.get_default()
        if display:
            Gtk.StyleContext.add_provider_for_display(
                display,
                self.css_provider,
                Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION,
            )

        self.default_tag.set_property("foreground", foreground)
        self.red_tag.set_property("foreground", red)
        self.green_tag.set_property("foreground", green)

    def start_shell(self) -> None:
        shell_binary = Path(__file__).resolve().parent.parent / "bin" / "SimpleShell"
        if not shell_binary.exists():
            self.append_output(f"Binary not found: {shell_binary}\nRun `make` first.\n")
            return

        pid, master_fd = pty.fork()
        if pid == 0:
            env = {**os.environ, "SIMPLESHELL_NO_TTY_EDITOR": "1"}
            os.execve(str(shell_binary), [str(shell_binary)], env)

        self.shell_pid = pid
        self.master_fd = master_fd
        self.exit_code = None

        self.stop_reader.clear()
        self.reader_thread = threading.Thread(target=self.shell_reader_loop, daemon=True)
        self.reader_thread.start()
        self.append_output("[SimpleShell started]\n")
        self.scroll_to_end()

    def shell_reader_loop(self) -> None:
        while not self.stop_reader.is_set():
            if self.master_fd is None:
                break
            try:
                ready, _, _ = select.select([self.master_fd], [], [], 0.2)
                if not ready:
                    continue
                data = os.read(self.master_fd, 4096)
                if not data:
                    break
                text = self.output_decoder.decode(data)
                GLib.idle_add(self.append_output, text)
            except OSError as error:
                if error.errno != errno.EIO:
                    GLib.idle_add(self.append_output, f"\n[PTY read error: {error}]\n")
                break

        self.reap_shell()
        if self.exit_code is not None:
            GLib.idle_add(
                self.append_output,
                f"\n[SimpleShell exited with code {self.exit_code}]\n",
            )

    def stop_shell(self) -> None:
        self.stop_reader.set()
        if self.shell_pid is not None and self.reap_shell() is None:
            try:
                os.killpg(self.shell_pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
            for _ in range(5):
                if self.reap_shell() is not None:
                    break
                time.sleep(0.05)
            if self.reap_shell() is None:
                try:
                    os.killpg(self.shell_pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                self.reap_shell()

        if self.master_fd is not None:
            try:
                os.close(self.master_fd)
            except OSError:
                pass
            self.master_fd = None

        if self.reader_thread and self.reader_thread.is_alive():
            self.reader_thread.join(timeout=0.2)

        self.shell_pid = None
        self.reader_thread = None

    def reap_shell(self) -> int | None:
        if self.shell_pid is None:
            return self.exit_code
        try:
            waited_pid, status = os.waitpid(self.shell_pid, os.WNOHANG)
        except ChildProcessError:
            return self.exit_code
        if waited_pid == 0:
            return None

        if os.WIFEXITED(status):
            self.exit_code = os.WEXITSTATUS(status)
        elif os.WIFSIGNALED(status):
            self.exit_code = 128 + os.WTERMSIG(status)
        else:
            self.exit_code = 1
        return self.exit_code

    def on_clear_clicked(self, _button: Gtk.Button) -> None:
        self.clear_output()

    def on_toggle_theme_clicked(self, _button: Gtk.Button) -> None:
        self.theme = "light" if self.theme == "dark" else "dark"
        self.apply_theme()

    def on_font_decrease_clicked(self, _button: Gtk.Button) -> None:
        self.set_font_size(self.font_size - 1)

    def on_font_increase_clicked(self, _button: Gtk.Button) -> None:
        self.set_font_size(self.font_size + 1)

    def on_console_clicked(self, _gesture: Gtk.GestureClick, _n_press: int, _x: float, _y: float) -> None:
        self.text_view.grab_focus()
        self.scroll_to_end()

    def on_key_pressed(
        self,
        _controller: Gtk.EventControllerKey,
        keyval: int,
        _keycode: int,
        state: Gdk.ModifierType,
    ) -> bool:
        if not self.is_shell_running():
            return True

        if keyval == Gdk.KEY_Return or keyval == Gdk.KEY_KP_Enter:
            self.write_to_shell("\n")
            return True

        if keyval == Gdk.KEY_BackSpace:
            self.write_to_shell("\x7f")
            return True

        if keyval == Gdk.KEY_l and state & Gdk.ModifierType.CONTROL_MASK:
            self.clear_output()
            return True

        if keyval in (Gdk.KEY_plus, Gdk.KEY_KP_Add) and state & Gdk.ModifierType.CONTROL_MASK:
            self.set_font_size(self.font_size + 1)
            return True

        if keyval in (Gdk.KEY_minus, Gdk.KEY_KP_Subtract) and state & Gdk.ModifierType.CONTROL_MASK:
            self.set_font_size(self.font_size - 1)
            return True

        text = Gdk.keyval_to_unicode(keyval)
        if text and text >= 32 and not state & Gdk.ModifierType.CONTROL_MASK:
            self.write_to_shell(chr(text))
            return True

        return True

    def is_shell_running(self) -> bool:
        return self.master_fd is not None and self.reap_shell() is None

    def write_to_shell(self, text: str) -> None:
        if self.master_fd is None:
            return
        try:
            os.write(self.master_fd, text.encode("utf-8"))
        except OSError:
            self.append_output("\n[Unable to write to shell process]\n")

    def append_output(self, text: str) -> bool:
        text = self.pending_output + text
        self.pending_output = ""

        index = 0
        while index < len(text):
            char = text[index]
            if char == "\x1b":
                next_index = self.handle_escape_sequence(text, index)
                if next_index is None:
                    self.pending_output = text[index:]
                    break
                index = next_index
                continue

            if char == "\r" and index + 1 < len(text) and text[index + 1] == "\n":
                index += 1
                continue

            if char == "\r":
                self.delete_current_line()
                index += 1
                continue

            if char == "\a":
                index += 1
                continue

            if char == "\b" or char == "\x7f":
                self.delete_previous_char()
                index += 1
                continue

            if ord(char) < 32 and char not in ("\n", "\t"):
                index += 1
                continue

            self.insert_text(char, self.current_output_tag)
            index += 1

        self.scroll_to_end()
        return False

    def handle_escape_sequence(self, text: str, index: int) -> int | None:
        if index + 1 >= len(text):
            return None

        sequence_type = text[index + 1]
        if sequence_type == "[":
            return self.handle_csi_sequence(text, index)
        if sequence_type == "]":
            return self.handle_osc_sequence(text, index)

        return index + 2

    def handle_csi_sequence(self, text: str, index: int) -> int | None:
        cursor = index + 2
        while cursor < len(text):
            final = text[cursor]
            if "@" <= final <= "~":
                payload = text[index + 2:cursor]
                self.apply_csi_sequence(payload, final)
                return cursor + 1
            cursor += 1
        return None

    def handle_osc_sequence(self, text: str, index: int) -> int | None:
        cursor = index + 2
        while cursor < len(text):
            if text[cursor] == "\a":
                return cursor + 1
            if text[cursor] == "\x1b":
                if cursor + 1 >= len(text):
                    return None
                if text[cursor + 1] == "\\":
                    return cursor + 2
            cursor += 1
        return None

    def apply_csi_sequence(self, payload: str, final: str) -> None:
        if final == "m":
            self.apply_sgr_sequence(payload)
            return
        if final == "K":
            self.delete_to_line_end()

    def apply_sgr_sequence(self, payload: str) -> None:
        parts = payload.split(";") if payload else ["0"]
        for part in parts:
            if part in ("", "0", "39"):
                self.current_output_tag = self.default_tag
            elif part == "31":
                self.current_output_tag = self.red_tag
            elif part == "32":
                self.current_output_tag = self.green_tag

    def delete_previous_char(self) -> None:
        end = self.buffer.get_end_iter()
        start = self.buffer.get_end_iter()
        if start.backward_char():
            self.buffer.delete(start, end)

    def delete_current_line(self) -> None:
        end = self.buffer.get_end_iter()
        start = self.buffer.get_end_iter()
        start.set_line_offset(0)
        self.buffer.delete(start, end)

    def delete_to_line_end(self) -> None:
        # The UI appends output at the logical cursor, so erase-to-end is equivalent
        # to clearing the current line tail from the current insertion point.
        end = self.buffer.get_end_iter()
        start = self.buffer.get_end_iter()
        self.buffer.delete(start, end)

    def insert_text(self, text: str, tag: Gtk.TextTag) -> None:
        for char in text:
            if char == "\b" or char == "\x7f":
                self.delete_previous_char()
                continue
            end = self.buffer.get_end_iter()
            self.buffer.insert_with_tags(end, char, tag)

    def clear_output(self) -> None:
        start = self.buffer.get_start_iter()
        end = self.buffer.get_end_iter()
        self.buffer.delete(start, end)
        self.scroll_to_end()

    def scroll_to_end(self) -> None:
        end = self.buffer.get_end_iter()
        self.buffer.place_cursor(end)
        mark = self.buffer.get_insert()
        self.text_view.scroll_mark_onscreen(mark)

    def set_font_size(self, size: int) -> None:
        self.font_size = max(8, min(28, size))
        self.apply_theme()

    def on_close_request(self, _window: Gtk.ApplicationWindow) -> bool:
        self.save_settings()
        self.stop_shell()
        return False


class SimpleShellGtkApp(Gtk.Application):
    def __init__(self) -> None:
        super().__init__(application_id="local.simpleshell.ui")

    def do_activate(self) -> None:
        window = SimpleShellGtkWindow(self)
        window.present()


def main() -> None:
    app = SimpleShellGtkApp()
    raise SystemExit(app.run(None))


if __name__ == "__main__":
    main()
