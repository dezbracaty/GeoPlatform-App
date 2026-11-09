#!/usr/bin/env python3
"""Journal recorder and App acceptance workbench."""
import argparse
import importlib
import json
import queue
import subprocess
import sys
import threading
import uuid
import webbrowser
from datetime import datetime
from pathlib import Path
import tkinter as tk
from tkinter import filedialog, messagebox
import customtkinter as ctk
from PIL import Image, ImageDraw

TOOLS_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS_DIR))
from journalsystem import JournalSystem
from journal_runner import JournalRunner, load_journal
from journal_execution_session import JournalExecutionSession
from session_repository import JournalSessionRepository

STATUS = {"passed": "通过", "failed": "失败", "blocked": "阻塞", "not_verified": "未验收"}


class JournalSystemGUI:
    def __init__(self, root, port=9999, application=None, app_root=None):
        self.root = root
        self.app_root = Path(app_root).resolve() if app_root else TOOLS_DIR.parents[1]
        self.client = self.runner = None
        self.busy = False
        self.task = ""
        self.cases, self.records = [], []
        self.selected_case = self.selected_record = None
        self.last_report = self.last_output = self.record_report = None
        self.last_batch_output = None
        self.cancel_event = threading.Event()
        self.events = queue.Queue()
        self._close_requested = False
        self._closed = False
        self.application_var = tk.StringVar(value=str(application or ""))
        self.program_label_var = tk.StringVar()
        self.port_var = tk.StringVar(value=str(port))
        self.exports_var = tk.StringVar()
        self.progress_var = tk.StringVar(value="就绪")
        self.connection_var = tk.StringVar(value="未连接 · 请先启动 GPlatform")
        self.record_name_var = tk.StringVar()
        self.record_status_var = tk.StringVar(value="未开始")
        self.repository = JournalSessionRepository(
            recordings=JournalSessionRepository.default_paths(application or ".")["recordings"],
            scenarios=self.app_root / "tests/TestAssets/scenarios")
        self._build_ui()
        self.search_entry.bind("<KeyRelease>", lambda _: self._show_cases())
        self.root.protocol("WM_DELETE_WINDOW", self.close)
        self.root.bind("<F5>", lambda _: self.refresh_current())
        self._event_timer = self.root.after(50, self._drain_events)
        self._load_timer = self.root.after(100, self.refresh_cases)
        self._update_controls()

    @property
    def recording(self):
        return bool(self.runner and self.runner.recording)

    @property
    def pending_save(self):
        return bool(self.runner and self.runner.has_pending_save)

    def _label(self, parent, text="", *, size=13, color="#697b96", bold=False, **kwargs):
        return ctk.CTkLabel(parent, text=text, text_color=color, anchor="w",
                           font=ctk.CTkFont(self.font_family, size + 1, weight="bold" if bold else "normal"), **kwargs)

    def _button(self, parent, text, command, *, kind="secondary", **kwargs):
        colors = {
            "primary": ("#5264e7", "#4354d5", "#ffffff", 0),
            "secondary": ("#ffffff", "#f3f5fb", "#7b87a1", 1),
            "quiet": ("transparent", "#eef1f8", "#8e9bb1", 0),
            "danger": ("#fff7f7", "#fceef0", "#bb6876", 1),
        }
        background, hover, foreground, border = colors[kind]
        return ctk.CTkButton(parent, text=text, command=command, height=35, width=98,
                            fg_color=background, hover_color=hover, text_color=foreground,
                            text_color_disabled="#b5bfd0", border_width=border, border_color="#e7ebf3",
                            corner_radius=7, font=ctk.CTkFont(self.font_family, 13), **kwargs)

    def _icon(self, name, color="#9ca8bc"):
        key = (name, color)
        if key not in self.icons:
            image = Image.new("RGBA", (72, 72))
            draw = ImageDraw.Draw(image)
            paths = {
                "cases": [[(4, 4), (20, 4), (20, 20), (4, 20), (4, 4)], [(8, 12), (11, 15), (16, 9)]],
                "record": [], "search": [[(16, 16), (21, 21)]],
                "file": [[(14, 3), (6, 3), (6, 21), (18, 21), (18, 7), (14, 3), (14, 8), (18, 8)],
                         [(9, 13), (15, 13)], [(9, 17), (13, 17)]],
                "folder": [[(3, 7), (10, 7), (12, 9), (21, 9), (21, 20), (3, 20), (3, 4), (10, 4), (12, 7)]],
                "refresh": [[(19, 3), (21, 9), (15, 9)], [(5, 21), (3, 15), (9, 15)]],
                "plus": [[(12, 5), (12, 19)], [(5, 12), (19, 12)]],
                "play": [[(8, 5), (19, 12), (8, 19), (8, 5)]],
                "link": [[(8, 3), (8, 8)], [(16, 3), (16, 8)],
                         [(6, 8), (18, 8), (18, 11), (15, 16), (9, 16), (6, 11), (6, 8)], [(12, 16), (12, 21)]],
                "screen": [[(3, 4), (21, 4), (21, 17), (3, 17), (3, 4)], [(12, 17), (12, 21)], [(8, 21), (16, 21)]],
                "db": [[(5, 5), (5, 19)], [(19, 5), (19, 19)]],
            }
            for path in paths[name]:
                draw.line([(x * 3, y * 3) for x, y in path], fill=color, width=4, joint="curve")
            if name in ("record", "search"):
                box = (4, 4, 20, 20) if name == "record" else (3, 3, 17, 17)
                draw.ellipse(tuple(v * 3 for v in box), outline=color, width=4)
                if name == "record":
                    draw.ellipse((27, 27, 45, 45), fill=color)
            elif name == "db":
                for y in (2, 9, 16):
                    draw.ellipse((15, y * 3, 57, (y + 6) * 3), outline=color, width=4)
            elif name == "refresh":
                draw.arc((12, 12, 60, 60), 195, 345, fill=color, width=4)
                draw.arc((12, 12, 60, 60), 15, 165, fill=color, width=4)
            self.icons[key] = ctk.CTkImage(light_image=image, dark_image=image, size=(17, 17))
        return self.icons[key]

    def _build_ui(self):
        ctk.set_appearance_mode("light")
        self.font_family = "Microsoft YaHei UI" if sys.platform.startswith("win") else (
            "PingFang SC" if sys.platform == "darwin" else "DejaVu Sans")
        self.icons = {}
        self.current_page = "test"
        self.case_rows, self.record_rows = {}, {}
        self.root.title("Journal · 场景测试与录制")
        self.root.geometry("1180x780")
        self.root.minsize(980, 640)
        self.root.configure(fg_color="#ffffff")
        self.root.columnconfigure(1, weight=1)
        self.root.rowconfigure(1, weight=1)

        rail = ctk.CTkFrame(self.root, width=156, fg_color="#f8fafd", corner_radius=0)
        rail.grid(row=0, column=0, rowspan=4, sticky="nsew")
        rail.grid_propagate(False)
        rail.columnconfigure(0, weight=1)
        rail.rowconfigure(4, weight=1)
        brand = ctk.CTkFrame(rail, fg_color="transparent")
        brand.grid(row=0, column=0, sticky="ew", padx=20, pady=(26, 32))
        mark = ctk.CTkFrame(brand, width=30, height=30, corner_radius=9, fg_color="#5264e7")
        mark.pack(side=tk.LEFT, padx=(0, 9))
        self._label(mark, "J", size=19, color="#ffffff", bold=True).place(relx=.5, rely=.5, anchor="center")
        wordmark = ctk.CTkFrame(brand, fg_color="transparent")
        wordmark.pack(side=tk.LEFT)
        self._label(wordmark, "Journal", size=16, bold=True, color="#354158", height=21).pack(anchor="w")
        self._label(wordmark, "GPLATFORM", size=8, color="#9ba7bc", height=15).pack(anchor="w")
        self._label(rail, "工 作 区", size=10, color="#a6b0c0").grid(row=1, column=0, sticky="w", padx=25)
        navigation = ctk.CTkFrame(rail, fg_color="transparent")
        navigation.grid(row=2, column=0, sticky="ew", padx=12, pady=(10, 0))
        navigation.columnconfigure(0, weight=1)
        self.nav_buttons = {}
        for row, (name, title, icon) in enumerate((("test", "场景测试", "cases"), ("record", "操作录制", "record"))):
            button = self._button(navigation, title, lambda page=name: self.show_page(page),
                                  kind="quiet", image=self._icon(icon), anchor="w")
            button.grid(row=row, column=0, sticky="ew", pady=3, ipady=3)
            self.nav_buttons[name] = button
        self._label(rail, "●  本地工作区\n测试与录制工具", size=10, color="#98a7bc",
                    justify="left").grid(row=5, column=0, sticky="w", padx=23, pady=22)
        ctk.CTkFrame(self.root, width=1, corner_radius=0, fg_color="#e9edf3").grid(
            row=0, column=0, rowspan=4, sticky="nse")

        header = ctk.CTkFrame(self.root, corner_radius=0, fg_color="#ffffff", height=83)
        header.grid(row=0, column=1, sticky="ew")
        header.grid_columnconfigure(0, weight=1)
        title = ctk.CTkFrame(header, fg_color="transparent")
        title.grid(row=0, column=0, sticky="w", padx=28, pady=17)
        self.breadcrumb = self._label(title, "Journal  /  场景测试", size=10, color="#a1abbd", height=19)
        self.breadcrumb.pack(anchor="w")
        self.page_heading = self._label(title, "场景测试", size=15, color="#46546c", bold=True, height=26)
        self.page_heading.pack(anchor="w")
        target = ctk.CTkFrame(header, fg_color="transparent")
        target.grid(row=0, column=1, sticky="e", padx=25)
        self._label(target, "", image=self._icon("file"), width=28).grid(row=0, column=0, rowspan=2, padx=(0, 8))
        self._label(target, "测试 / 回放程序", size=9, color="#9aa7bc", height=18).grid(row=0, column=1, sticky="w")
        self._label(target, "", textvariable=self.program_label_var, size=11, color="#6d7c95", height=21).grid(
            row=1, column=1, sticky="w")
        self.program_button = self._button(target, "更换程序", self.choose_application, kind="quiet")
        self.program_button.grid(row=0, column=2, rowspan=2, padx=(15, 0))
        ctk.CTkFrame(header, height=1, corner_radius=0, fg_color="#e9edf3").grid(
            row=1, column=0, columnspan=2, sticky="ew")

        self.pages = ctk.CTkFrame(self.root, fg_color="#ffffff", corner_radius=0)
        self.pages.grid(row=1, column=1, sticky="nsew")
        self.pages.columnconfigure(0, weight=1)
        self.pages.rowconfigure(0, weight=1)
        self.test_page = ctk.CTkFrame(self.pages, fg_color="#ffffff", corner_radius=0)
        self.record_page = ctk.CTkFrame(self.pages, fg_color="#ffffff", corner_radius=0)
        self._build_test_page()
        self._build_record_page()
        self.test_page.grid(row=0, column=0, sticky="nsew")

        footer = ctk.CTkFrame(self.root, fg_color="#fdfdff", corner_radius=0)
        footer.grid(row=2, column=1, sticky="ew")
        footer.columnconfigure(1, weight=1)
        ctk.CTkFrame(footer, height=1, fg_color="#edf0f5", corner_radius=0).grid(
            row=0, column=0, columnspan=2, sticky="ew")
        self.log_toggle = self._button(footer, "运行日志  ⌄", self.toggle_log, kind="quiet")
        self.log_toggle.grid(row=1, column=0, padx=18, pady=3)
        self._label(footer, "", textvariable=self.progress_var, size=10, color="#8699b4",
                    wraplength=600, justify="right").grid(row=1, column=1, sticky="e", padx=23)
        self.log_frame = ctk.CTkFrame(self.root, fg_color="#fdfdff", corner_radius=0)
        self.log_frame.columnconfigure(0, weight=1)
        self.log_text = ctk.CTkTextbox(self.log_frame, height=106, wrap="word", state="disabled",
                                     font=ctk.CTkFont("Consolas", 11), fg_color="#f8fafe",
                                     text_color="#8898b1", border_width=0, corner_radius=7)
        self.log_text.grid(sticky="ew", padx=22, pady=(0, 12))
        self.log("场景测试采用 App 统一验收；录制回放只显示诊断结果。")
        self.show_page("test")

    def show_page(self, page):
        if self.busy or self.recording or self.pending_save:
            return
        self.current_page = page
        self.test_page.grid_remove()
        self.record_page.grid_remove()
        (self.test_page if page == "test" else self.record_page).grid(row=0, column=0, sticky="nsew")
        title = "场景测试" if page == "test" else "操作录制"
        self.breadcrumb.configure(text="Journal  /  " + title)
        self.page_heading.configure(text=title)
        self._update_navigation()

    def _update_navigation(self):
        locked = self.busy or self.recording or self.pending_save
        for name, button in self.nav_buttons.items():
            selected = self.current_page == name
            button.configure(fg_color="#eceffd" if selected else "transparent",
                             text_color="#596bd2" if selected else "#8592a8",
                             state="disabled" if locked and not selected else "normal")

    def _list_row(self, parent, name, detail, command, icon):
        frame = ctk.CTkFrame(parent, corner_radius=7, fg_color="transparent")
        frame.pack(fill=tk.X, pady=3)
        caption = name
        font = ctk.CTkFont(self.font_family, 13)
        while caption and font.measure(caption) > 164:
            caption = caption[:-1]
        if caption != name:
            caption += "…"
        button = self._button(frame, caption, command, kind="quiet", image=self._icon(icon), anchor="w")
        button.pack(fill=tk.X, padx=3, pady=(4, 0))
        label = self._label(frame, detail, size=10, color="#8998b0", height=22)
        label.pack(fill=tk.X, padx=(39, 10), pady=(0, 8))
        label.bind("<Button-1>", lambda _: button.invoke())
        return frame, button, label

    def _highlight_rows(self, rows, selected):
        for index, (frame, button, label) in rows.items():
            active = index == selected
            background, foreground = ("#edf0fd", "#5b69c3") if active else ("transparent", "#697890")
            if frame.cget("fg_color") != background:
                frame.configure(fg_color=background)
            if button.cget("text_color") != foreground:
                button.configure(text_color=foreground)

    def _wrap_details(self, event, labels):
        width = max(250, int(event.width / ctk.ScalingTracker.get_widget_scaling(self.root)) - 12)
        for label in labels:
            if label.cget("wraplength") != width:
                label.configure(wraplength=width)

    def _build_test_page(self):
        self.test_page.columnconfigure(1, weight=1)
        self.test_page.rowconfigure(0, weight=1)
        left = ctk.CTkFrame(self.test_page, width=254, fg_color="#fcfdff", corner_radius=0)
        left.grid(row=0, column=0, sticky="nsew")
        left.grid_propagate(False)
        left.columnconfigure(0, weight=1)
        left.rowconfigure(2, weight=1)
        bar = ctk.CTkFrame(left, fg_color="transparent")
        bar.grid(row=0, column=0, sticky="ew", padx=18, pady=(24, 13))
        bar.columnconfigure(0, weight=1)
        self.case_count = self._label(bar, "测试场景", size=12, color="#71829c")
        self.case_count.grid(row=0, column=0, sticky="w")
        self.refresh_button = self._button(bar, "", self.refresh_cases, kind="quiet",
                                           image=self._icon("refresh"))
        self.refresh_button.configure(width=29)
        self.refresh_button.grid(row=0, column=1)
        self.search_entry = ctk.CTkEntry(left, placeholder_text="搜索场景…",
                                        height=35, corner_radius=7, border_width=0,
                                        fg_color="#f1f4f9", text_color="#687890",
                                        font=ctk.CTkFont(self.font_family, 13))
        self.search_entry.grid(row=1, column=0, sticky="ew", padx=18, pady=(0, 16))
        self.case_list = ctk.CTkScrollableFrame(left, fg_color="transparent", corner_radius=0,
                                               scrollbar_button_color="#e1e6f0",
                                               scrollbar_button_hover_color="#cbd4e5")
        self.case_list.grid(row=2, column=0, sticky="nsew", padx=(10, 5))
        self.case_directory_button = self._button(left, "场景目录", self.choose_case_directory,
                                                   kind="quiet", image=self._icon("folder"), anchor="w")
        self.case_directory_button.grid(row=3, column=0, sticky="w", padx=15, pady=14)
        ctk.CTkFrame(self.test_page, width=1, corner_radius=0, fg_color="#e9edf3").grid(
            row=0, column=0, sticky="nse")

        right = ctk.CTkScrollableFrame(self.test_page, fg_color="#ffffff", corner_radius=0,
                                      scrollbar_button_color="#e8ecf4", scrollbar_button_hover_color="#d8dfeb")
        right.grid(row=0, column=1, sticky="nsew", padx=(29, 19), pady=(25, 15))
        right.columnconfigure(0, weight=1)
        self._label(right, "—  SCENARIO", size=9, color="#a7b1c3").grid(row=0, column=0, sticky="w")
        self.case_title = self._label(right, "选择测试场景", size=23, color="#36435b", bold=True,
                                      wraplength=540, justify="left")
        self.case_title.grid(row=1, column=0, sticky="ew", pady=(6, 0))
        self.case_id_label = self._label(right, "", size=10, color="#8a9ab2")
        self.case_id_label.grid(row=2, column=0, sticky="w", pady=(2, 20))
        info = ctk.CTkFrame(right, fg_color="transparent")
        info.grid(row=3, column=0, sticky="ew")
        info.columnconfigure(0, weight=1)
        self._label(info, "验收目标", size=11, color="#7d90ab").grid(row=0, column=0, sticky="w")
        self.case_target = self._label(info, "从左侧选择场景，查看目标与准备情况。",
                                       wraplength=540, justify="left", size=12, color="#667890")
        self.case_target.grid(row=1, column=0, sticky="ew", pady=(7, 19))
        facts = ctk.CTkFrame(info, corner_radius=8, fg_color="#f7f9fc", border_width=1, border_color="#f0f3f8")
        facts.grid(row=2, column=0, sticky="ew")
        facts.columnconfigure(0, weight=1)
        self._label(facts, "验收方式", size=10, color="#8699b4").grid(row=0, column=0, sticky="w", padx=17, pady=(11, 0))
        self.case_method = self._label(facts, "", size=12, color="#697d99")
        self.case_method.grid(row=1, column=0, sticky="w", padx=17, pady=(1, 11))
        self.case_readiness = self._label(info, "", wraplength=540, justify="left", size=11)
        self.case_readiness.grid(row=3, column=0, sticky="w", pady=(13, 0))
        self.export_frame = ctk.CTkFrame(info, fg_color="transparent")
        self.export_frame.columnconfigure(0, weight=1)
        self._label(self.export_frame, "导出类场景需要 App 实际导出目录，批量回归也会使用。", size=11).grid(row=0, column=0, sticky="w")
        self._label(self.export_frame, "", textvariable=self.exports_var, wraplength=350,
                     size=10).grid(row=1, column=0, sticky="w")
        self.exports_button = self._button(self.export_frame, "选择导出目录", self.choose_exports)
        self.exports_button.grid(row=0, column=1, rowspan=2, padx=(8, 0))

        actions = ctk.CTkFrame(right, fg_color="transparent")
        actions.grid(row=4, column=0, sticky="ew", pady=(22, 12))
        self.run_button = self._button(actions, "运行场景", self.run_case, kind="primary",
                                       image=self._icon("play", "#ffffff"))
        self.run_button.pack(side=tk.LEFT)
        self.batch_button = self._button(actions, "批量回归测试", self.run_batch)
        self.batch_button.configure(width=142)
        self.batch_button.pack(side=tk.LEFT, padx=(8, 0))
        self.cancel_button = self._button(actions, "取消运行", self.cancel, kind="danger")
        self.cancel_button.pack(side=tk.LEFT, padx=8)
        self.run_hint = self._label(right, "", size=10, wraplength=540, color="#8496b0")
        self.run_hint.grid(row=5, column=0, sticky="w")
        self.execution_progress = ctk.CTkProgressBar(right, height=3, mode="indeterminate",
                                                    progress_color="#7180e4", fg_color="#edf0fb")
        ctk.CTkFrame(right, height=1, corner_radius=0, fg_color="#edf0f5").grid(
            row=7, column=0, sticky="ew", pady=24)
        self._label(right, "最近运行", size=12, color="#75849e", bold=True).grid(row=8, column=0, sticky="w")
        results = ctk.CTkFrame(right, corner_radius=9, fg_color="#fbfcff", border_width=1, border_color="#eaf0f6")
        results.grid(row=9, column=0, sticky="ew", pady=(12, 14))
        results.columnconfigure(0, weight=1)
        self.result_label = self._label(results, "尚未运行。", wraplength=500, justify="left", size=12)
        self.result_label.grid(row=0, column=0, sticky="w", padx=18, pady=(13, 8))
        result_actions = ctk.CTkFrame(results, fg_color="transparent")
        result_actions.grid(row=1, column=0, sticky="w", padx=15, pady=(0, 12))
        self.report_button = self._button(result_actions, "查看报告", self.open_report, kind="quiet")
        self.report_button.pack(side=tk.LEFT)
        self.result_folder_button = self._button(result_actions, "打开结果目录", self.open_result_folder, kind="quiet")
        self.result_folder_button.pack(side=tk.LEFT, padx=4)
        self.batch_result_label = self._label(results, "尚未执行批量回归。", wraplength=500, justify="left", size=11)
        self.batch_result_label.grid(row=2, column=0, sticky="w", padx=18, pady=(3, 8))
        self.batch_review_button = self._button(results, "显示最近批量结果", self.open_batch_results, kind="quiet")
        self.batch_review_button.configure(width=160)
        self.batch_review_button.grid(row=3, column=0, sticky="w", padx=15, pady=(0, 12))
        self._label(right, "每次启动独立实例；完成后保存报告并清理实例。", size=10,
                     color="#8e9eb5", wraplength=540).grid(row=10, column=0, sticky="w")
        right.bind("<Configure>", lambda event: self._wrap_details(
            event, (self.case_title, self.case_target, self.case_readiness, self.result_label, self.batch_result_label, self.run_hint)))

    def _build_record_page(self):
        self.record_page.columnconfigure(1, weight=1)
        self.record_page.rowconfigure(0, weight=1)
        left = ctk.CTkFrame(self.record_page, width=254, fg_color="#fcfdff", corner_radius=0)
        left.grid(row=0, column=0, sticky="nsew")
        left.grid_propagate(False)
        left.columnconfigure(0, weight=1)
        left.rowconfigure(2, weight=1)
        bar = ctk.CTkFrame(left, fg_color="transparent")
        bar.grid(row=0, column=0, sticky="ew", padx=18, pady=(24, 13))
        bar.columnconfigure(0, weight=1)
        self._label(bar, "录制会话", size=12, color="#71829c").grid(row=0, column=0, sticky="w")
        self.new_record_button = self._button(bar, "新录制", self.new_recording, kind="quiet",
                                              image=self._icon("plus"))
        self.new_record_button.configure(width=80)
        self.new_record_button.grid(row=0, column=1)
        self._label(left, "保存操作，留待后续回放检查。", size=10, color="#8998b0").grid(
            row=1, column=0, sticky="w", padx=21, pady=(0, 16))
        self.record_list = ctk.CTkScrollableFrame(left, fg_color="transparent", corner_radius=0,
                                                 scrollbar_button_color="#e1e6f0",
                                                 scrollbar_button_hover_color="#cbd4e5")
        self.record_list.grid(row=2, column=0, sticky="nsew", padx=(10, 5))
        self.record_folder_button = self._button(left, "打开录制目录", self.open_record_folder,
                                                 kind="quiet", image=self._icon("folder"), anchor="w")
        self.record_folder_button.grid(row=3, column=0, sticky="w", padx=15, pady=14)
        ctk.CTkFrame(self.record_page, width=1, corner_radius=0, fg_color="#e9edf3").grid(
            row=0, column=0, sticky="nse")
        right = ctk.CTkScrollableFrame(self.record_page, corner_radius=0, fg_color="#ffffff",
                                      scrollbar_button_color="#e8ecf4", scrollbar_button_hover_color="#d8dfeb")
        right.grid(row=0, column=1, sticky="nsew", padx=(29, 19), pady=(25, 15))
        right.columnconfigure(0, weight=1)
        self._label(right, "—  RECORDER", size=9, color="#a7b1c3").grid(row=0, column=0, sticky="w")
        self.record_heading = self._label(right, "录制新的操作", size=23, color="#36435b", bold=True)
        self.record_heading.grid(row=1, column=0, sticky="w", pady=(6, 1))
        self._label(right, "将实际操作保存为可复现的记录。", size=11, color="#8799b1").grid(
            row=2, column=0, sticky="w", pady=(0, 23))
        self.editor = ctk.CTkFrame(right, fg_color="transparent")
        self.editor.grid(row=3, column=0, sticky="ew")
        self.editor.columnconfigure(0, weight=1)
        connection = ctk.CTkFrame(self.editor, corner_radius=9, border_width=1,
                                  border_color="#e9edf5", fg_color="#fbfcff")
        connection.grid(row=0, column=0, sticky="ew")
        connection.columnconfigure(1, weight=1)
        self._label(connection, "", image=self._icon("link", "#8694d4"), width=31).grid(
            row=0, column=0, rowspan=2, padx=(16, 9), pady=18)
        self._label(connection, "连接 GPlatform", size=12, color="#77859f", bold=True).grid(
            row=0, column=1, sticky="w", pady=(15, 0))
        self._label(connection, "", textvariable=self.connection_var, size=10, color="#8597b0",
                     wraplength=330, justify="left").grid(row=1, column=1, sticky="w", pady=(0, 10))
        self.connect_button = self._button(connection, "连接程序", self.toggle_connection)
        self.connect_button.grid(row=0, column=2, rowspan=2, padx=17, pady=18)
        self._button(connection, "连接设置  ⌄", self.toggle_connection_options, kind="quiet").grid(
            row=2, column=1, sticky="w", pady=(0, 9))
        self.connection_options = ctk.CTkFrame(connection, fg_color="transparent")
        self.connection_options.grid(row=3, column=1, sticky="w", pady=(0, 12))
        self._label(self.connection_options, "本机端口", size=11).pack(side=tk.LEFT)
        self.port_entry = ctk.CTkEntry(self.connection_options, textvariable=self.port_var, width=90, height=30,
                                      font=ctk.CTkFont(self.font_family, 13), fg_color="#ffffff",
                                      border_color="#e2e7f0", border_width=1)
        self.port_entry.pack(side=tk.LEFT, padx=12)
        self.connection_options.grid_remove()
        form = ctk.CTkFrame(self.editor, fg_color="#ffffff", border_width=1,
                            border_color="#e9edf5", corner_radius=9)
        form.grid(row=1, column=0, sticky="ew", pady=20)
        form.columnconfigure(0, weight=1)
        self._label(form, "① 连接程序    —    ② 记录操作    —    ③ 停止保存", size=11,
                     color="#8799b2").grid(row=0, column=0, sticky="w", padx=21, pady=(20, 20))
        self._label(form, "录制名称", size=11, color="#7185a1").grid(row=1, column=0, sticky="w", padx=21)
        self._label(form, "可选，留空时使用录制时间", size=10, color="#91a1b8").grid(row=2, column=0, sticky="w", padx=21)
        self.name_entry = ctk.CTkEntry(form, textvariable=self.record_name_var, height=37,
                                      placeholder_text="例如：小模型导入确认", border_width=1,
                                      border_color="#e3e9f2", fg_color="#ffffff",
                                      font=ctk.CTkFont(self.font_family, 13), text_color="#718199")
        self.name_entry.grid(row=3, column=0, sticky="ew", padx=21, pady=(9, 20))
        controls = ctk.CTkFrame(form, fg_color="transparent")
        controls.grid(row=4, column=0, sticky="w", padx=21)
        self.record_button = self._button(controls, "开始录制", self.start_recording, kind="primary",
                                          image=self._icon("record", "#ffffff"))
        self.record_button.pack(side=tk.LEFT)
        self.stop_button = self._button(controls, "停止并保存", self.stop_recording, kind="primary")
        self.stop_button.pack(side=tk.LEFT)
        self.checkpoint_button = self._button(controls, "添加检查点", self.checkpoint_dialog)
        self.checkpoint_button.pack(side=tk.LEFT, padx=8)
        self._label(form, "", textvariable=self.record_status_var, wraplength=480, size=11,
                     color="#b07e89", justify="left").grid(row=5, column=0, sticky="w", padx=21, pady=(12, 17))
        self._label(self.editor, "检查点保存为候选数据，核对后再纳入正式场景标准。", size=10,
                     color="#8a9ab2", wraplength=540).grid(row=2, column=0, sticky="w")
        self.saved_panel = ctk.CTkFrame(right, fg_color="transparent")
        self.saved_panel.columnconfigure(0, weight=1)
        self.saved_name = self._label(self.saved_panel, "", size=17, color="#677893", bold=True,
                                      wraplength=540, justify="left")
        self.saved_name.grid(row=0, column=0, sticky="w", pady=(0, 16))
        self._label(self.saved_panel, "这是一份操作录制，尚不代表正式场景验收通过。", size=11,
                     wraplength=540).grid(row=1, column=0, sticky="w")
        saved_controls = ctk.CTkFrame(self.saved_panel, fg_color="transparent")
        saved_controls.grid(row=2, column=0, sticky="w", pady=22)
        self.replay_button = self._button(saved_controls, "回放检查", self.replay_record, kind="primary",
                                          image=self._icon("play", "#ffffff"))
        self.replay_button.pack(side=tk.LEFT)
        self.cancel_replay_button = self._button(saved_controls, "取消回放", self.cancel, kind="danger")
        self.cancel_replay_button.pack(side=tk.LEFT, padx=8)
        self.saved_folder_button = self._button(saved_controls, "打开目录", self.open_record_folder)
        self.saved_folder_button.pack(side=tk.LEFT, padx=4)
        self.record_result = self._label(self.saved_panel, "回放启动独立实例，不重启当前录制连接中的程序。",
                                         wraplength=540, justify="left", size=12)
        self.replay_progress = ctk.CTkProgressBar(self.saved_panel, height=3, mode="indeterminate",
                                                progress_color="#7180e4", fg_color="#edf0fb")
        self.record_result.grid(row=4, column=0, sticky="w")
        self.record_report_button = self._button(self.saved_panel, "查看诊断报告", self.open_record_report, kind="quiet")
        self.record_report_button.grid(row=5, column=0, sticky="w", pady=16)
        right.bind("<Configure>", lambda event: self._wrap_details(event, (self.saved_name, self.record_result)))

    def log(self, message):
        self.log_text.configure(state="normal")
        self.log_text.insert(tk.END, f"{datetime.now():%H:%M:%S}  {message}\n")
        self.log_text.see(tk.END)
        self.log_text.configure(state="disabled")

    def toggle_log(self):
        if self.log_frame.winfo_manager():
            self.log_frame.grid_remove()
            self.log_toggle.configure(text="运行日志  ⌄")
        else:
            self.log_frame.grid(row=3, column=1, sticky="ew")
            self.log_toggle.configure(text="运行日志  ⌃")

    def _connected(self):
        return bool(self.client and self.client.client)

    def _playbacks(self):
        application = self.application_var.get().strip()
        if application:
            return JournalSessionRepository.default_paths(application)["playbacks"]
        return self.repository.playbacks

    def _app_entry(self):
        expected = self.app_root / "tools/run_acceptance.py"
        if not expected.is_file():
            raise FileNotFoundError(f"缺少 App 统一验收入口：{expected}")
        if str(expected.parent) not in sys.path:
            sys.path.insert(0, str(expected.parent))
        module = importlib.import_module("run_acceptance")
        if Path(module.__file__).resolve() != expected.resolve():
            raise RuntimeError("当前进程已加载另一份 App 验收入口，请重启工具")
        return module

    def _start_task(self, kind, label, operation, finished):
        if self.busy:
            return
        self.busy, self.task = True, kind
        self.cancel_event.clear()
        self.progress_var.set(label)
        self._update_controls()
        def worker():
            try:
                self.events.put(("done", (finished, operation())))
            except Exception as exc:
                self.events.put(("error", exc))
        threading.Thread(target=worker, daemon=True).start()

    def _drain_events(self):
        try:
            while True:
                kind, payload = self.events.get_nowait()
                if kind == "log":
                    self.log(payload)
                elif kind == "progress":
                    self.progress_var.set(payload)
                elif kind == "done":
                    self.busy, self.task = False, ""
                    callback, result = payload
                    try:
                        callback(result)
                    except Exception as exc:
                        self._task_failed(exc)
                    self._update_controls()
                    if self._close_requested:
                        self.close()
                        if self._closed:
                            return
                elif kind == "error":
                    self.busy, self.task = False, ""
                    self._task_failed(payload)
                    if self._close_requested:
                        self.close()
                        if self._closed:
                            return
        except queue.Empty:
            pass
        if not self._closed:
            self._event_timer = self.root.after(50, self._drain_events)

    def _task_failed(self, error):
        if isinstance(error, (ConnectionError, TimeoutError)) and self.client:
            self.client.disconnect()
            self.client = None
            self.connection_var.set("连接中断 · 请重连同一实例")
        if self.pending_save:
            self.record_status_var.set("保存失败，内容仍保留。可重试保存，不要关闭工具。")
        else:
            self.progress_var.set(f"操作失败：{error}")
        self.log(f"操作失败：{error}")
        self._update_controls()
        messagebox.showerror("操作失败", str(error), parent=self.root)

    def _update_controls(self):
        locked = self.busy or self.recording or self.pending_save
        program = self.application_var.get().strip()
        valid_program = bool(program and Path(program).is_file())
        self.program_label_var.set(Path(program).name if program else "尚未选择")
        self.program_button.configure(state="disabled" if locked else "normal")
        self.program_button.configure(text="更换程序" if program else "选择程序")
        self._update_navigation()
        for widget in (self.refresh_button, self.case_directory_button, self.search_entry, self.exports_button):
            widget.configure(state="disabled" if locked else "normal")
        for rows in (self.case_rows, self.record_rows):
            for _, button, _ in rows.values():
                state = "disabled" if locked else "normal"
                if button.cget("state") != state:
                    button.configure(state=state)
        selected = self.selected_case
        needs_exports = bool(selected and selected["metadata"].get("artifacts"))
        ready = bool(selected and not selected.get("error"))
        self.run_button.configure(state="normal" if ready and valid_program and not locked and
                                  (not needs_exports or self.exports_var.get()) else "disabled")
        batch_needs_exports = any(item.get("metadata", {}).get("artifacts") for item in self.cases)
        self.batch_button.configure(state="normal" if valid_program and self.cases and not locked and
                                    (not batch_needs_exports or self.exports_var.get()) else "disabled")
        self.batch_review_button.configure(state="normal" if not locked and self.last_batch_output and
            (self.last_batch_output / "screenshot_review.json").is_file() else "disabled")
        for button in (self.cancel_button, self.cancel_replay_button):
            if self.busy and self.task in ("scene", "batch", "replay"):
                button.pack(side=tk.LEFT, padx=8)
                button.configure(state="disabled" if self.cancel_event.is_set() else "normal")
            else:
                button.pack_forget()
        hint = "请选择测试程序。" if not valid_program else (
            "请先准备缺少的场景资产。" if selected and selected.get("error") else (
            "请选择 App 实际导出目录。" if needs_exports and not self.exports_var.get() else
            "使用独立实例执行；结果以场景报告为准。"))
        self.run_hint.configure(text=hint)
        if needs_exports or batch_needs_exports:
            self.export_frame.grid(row=4, column=0, sticky="ew", pady=(12, 0))
        else:
            self.export_frame.grid_remove()
        self.report_button.configure(state="normal" if self.last_report and self.last_report.is_file() else "disabled")
        self.result_folder_button.configure(state="normal" if self.last_output and self.last_output.is_dir() else "disabled")
        self.new_record_button.configure(state="disabled" if locked else "normal")
        connected = self._connected()
        self.connect_button.configure(text="断开" if connected else "重新连接" if self.recording else "连接程序",
                                      state="disabled" if self.busy or self.pending_save or
                                      (self.recording and connected) else "normal")
        self.port_entry.configure(state="disabled" if connected or locked else "normal")
        self.name_entry.configure(state="disabled" if locked else "normal")
        if self.recording or self.pending_save:
            self.record_button.pack_forget()
            self.stop_button.pack(side=tk.LEFT, before=self.checkpoint_button)
        else:
            self.stop_button.pack_forget()
            self.record_button.pack(side=tk.LEFT, before=self.checkpoint_button)
        self.record_button.configure(state="normal" if connected and not locked else "disabled")
        self.stop_button.configure(text="重试保存" if self.pending_save else "停止并保存",
                                   state="normal" if not self.busy and
                                   (self.pending_save or self.recording and connected) else "disabled")
        self.checkpoint_button.configure(state="normal" if self.recording and connected and not self.busy else "disabled")
        self.replay_button.configure(state="normal" if self.selected_record and not self.selected_record.get("error") and valid_program and not locked else "disabled")
        self.record_report_button.configure(state="normal" if self.record_report and self.record_report.is_file() else "disabled")
        for button in (self.record_folder_button, self.saved_folder_button):
            button.configure(state="disabled" if self.busy else "normal")
        for button in (self.run_button, self.batch_button, self.record_button, self.stop_button, self.replay_button):
            button.configure(fg_color="#5264e7" if button.cget("state") == "normal" else "#e8ecf5")
        for bar, task, row in ((self.execution_progress, "scene", 6), (self.replay_progress, "replay", 3)):
            if self.busy and (self.task == task or task == "scene" and self.task == "batch"):
                if not bar.winfo_manager():
                    bar.grid(row=row, column=0, sticky="ew", pady=(12, 6))
                    bar.start()
            elif bar.winfo_manager():
                bar.stop()
                bar.grid_remove()

    def choose_application(self):
        name = filedialog.askopenfilename(title="选择本次测试的 GPlatform 可执行文件", parent=self.root)
        if name:
            self.application_var.set(name)
            self._show_latest_result()
            self._show_latest_batch()
            self._update_controls()

    def choose_exports(self):
        name = filedialog.askdirectory(title="选择 GPlatform 实际导出目录", parent=self.root)
        if name:
            self.exports_var.set(name)
            self._update_controls()

    def choose_case_directory(self):
        name = filedialog.askdirectory(title="选择测试资产中的 scenarios 目录", parent=self.root)
        if name:
            self.repository.scenarios = Path(name)
            self.refresh_cases()

    def refresh_current(self):
        if self.busy or self.recording or self.pending_save:
            return
        if self.current_page == "test":
            self.refresh_cases()
        else:
            self._show_records()

    def refresh_cases(self):
        def load():
            entry = self._app_entry()
            scenarios = []
            for item in JournalSessionRepository(scenarios=self.repository.scenarios).list_sessions():
                if item["type"] != "场景":
                    continue
                if not item["error"]:
                    try:
                        case = entry.load_case(self.repository.scenarios.parent, item["path"].name, load_journal)
                        item["plan"] = case["verificationPlan"]
                    except Exception as exc:
                        item["error"] = str(exc)
                scenarios.append(item)
            return scenarios
        self._start_task("load", "正在读取场景与准备状态…", load, self._cases_loaded)

    def _cases_loaded(self, cases):
        previous = self.selected_case["path"] if self.selected_case else None
        self.cases = cases
        self.case_count.configure(text=f"测试场景  {len(cases)}")
        for frame, _, _ in self.case_rows.values():
            frame.destroy()
        self.case_rows.clear()
        self._show_cases(previous)
        self._show_records()
        self._show_latest_batch()
        self.batch_button.configure(text=f"批量回归测试（{len(cases)}）", width=190)
        self.progress_var.set(f"已加载 {len(cases)} 个场景")

    def _show_cases(self, prefer=None):
        if self.busy or self.recording or self.pending_save:
            return
        prefer = prefer or (self.selected_case["path"] if self.selected_case else None)
        query = self.search_entry.get().strip().lower()
        for frame, _, _ in self.case_rows.values():
            frame.pack_forget()
        shown = []
        for index, item in enumerate(self.cases):
            if query and query not in (item["name"] + item["path"].name).lower():
                continue
            methods = item["metadata"].get("verification", {}).get("requiredMethods", [])
            detail = " · ".join({"db": "DB 数据对比", "screenshot": "标准截图对比"}.get(m, m) for m in methods)
            if item.get("error"):
                detail = "准备阻塞 · " + detail
            if index not in self.case_rows:
                self.case_rows[index] = self._list_row(
                    self.case_list, item["name"], detail or "待补充验收检查",
                    lambda i=index: self.select_case(i), "db" if "db" in methods else "screen")
            self.case_rows[index][0].pack(fill=tk.X, pady=3)
            shown.append(index)
        chosen = next((i for i in shown if self.cases[i]["path"] == prefer), shown[0] if shown else None)
        if chosen is not None:
            self.select_case(chosen)
        else:
            self.selected_case = None
            self.case_title.configure(text="没有匹配的场景" if self.cases else "暂无测试场景")
            self.case_id_label.configure(text="")
            self.case_target.configure(text="选择已有测试资产目录，或调整搜索内容。")
            self.case_method.configure(text="")
            self.case_readiness.configure(text="")
            self.last_report = self.last_output = None
            self.result_label.configure(text="尚未运行。")
            self._update_controls()

    def select_case(self, index):
        if self.busy or self.recording or self.pending_save:
            return
        self.selected_case = self.cases[index]
        self._highlight_rows(self.case_rows, index)
        item, meta = self.selected_case, self.selected_case["metadata"]
        self.case_title.configure(text=item["name"])
        self.case_id_label.configure(text=item["path"].name)
        self.case_target.configure(text=meta.get("acceptanceTarget") or meta.get("testPurpose") or "缺少验收目标。")
        methods = meta.get("verification", {}).get("requiredMethods", [])
        self.case_method.configure(text="、".join(
            {"db": "DB 数据对比", "screenshot": "标准截图对比"}.get(m, m) for m in methods) or "待补充验收检查")
        if item.get("error"):
            reason = item["error"]
            if "unmaterialized LFS pointer" in reason:
                reason = "标准截图尚未下载，请在测试资产目录执行 git lfs pull。"
            self.case_readiness.configure(text="准备阻塞：" + reason, text_color="#b28a40")
        elif item.get("plan", {}).get("missingMethods"):
            self.case_readiness.configure(text="缺少验收检查；可诊断回放，结果仍为未验收。", text_color="#b28a40")
        else:
            self.case_readiness.configure(text="●  场景与资产已就绪", text_color="#65a98b")
        self._show_latest_result()
        self._update_controls()

    def _show_latest_result(self):
        self.last_report = self.last_output = None
        base, case = self._playbacks(), self.selected_case
        paths = list(base.glob(f"session_*/{case['path'].name}/case_result.json")) if base and case else []
        if not paths:
            self.result_label.configure(text="尚未运行。完成后在这里查看本场景的结果。")
            return
        result_file = max(paths, key=lambda p: p.stat().st_mtime)
        try:
            result = json.loads(result_file.read_text(encoding="utf-8"))
            self.last_report, self.last_output = result_file.parent / "case_report.html", result_file.parent
            summary_path = result_file.parent.parent / "summary.json"
            summary = json.loads(summary_path.read_text(encoding="utf-8")) if summary_path.is_file() else {}
            error = summary.get("cleanupError") or summary.get("error") or result.get("error")
            status = summary.get("status", result.get("status"))
            if error == "Replay cancelled by caller":
                error = "已取消，未完成验收。"
            text = STATUS.get(status, "未知结果") + " · " + datetime.fromtimestamp(
                result_file.stat().st_mtime).strftime("%m-%d %H:%M")
            self.result_label.configure(text=text + ("\n" + error if error else ""))
        except (OSError, ValueError) as exc:
            self.result_label.configure(text=f"结果无法读取：{exc}")

    def _show_latest_batch(self):
        self.last_batch_output = None
        application = self.application_var.get().strip()
        assets = self.repository.scenarios.parent.resolve()
        candidates = list(self._playbacks().glob("session_*/summary.json")) if application else []
        for path in sorted(candidates, key=lambda p: p.stat().st_mtime, reverse=True):
            try:
                summary = json.loads(path.read_text(encoding="utf8"))
                if (summary.get("selectionScope") != "all" or
                        Path(summary.get("applicationPath", "")).resolve() != Path(application).resolve() or
                        Path(summary.get("assetsRoot", "")).resolve() != assets or
                        not (path.parent / "screenshot_review.json").is_file()):
                    continue
                self.last_batch_output = path.parent
                completed, total = summary.get("completedCaseCount", 0), summary.get("plannedCaseCount", 0)
                status = "已取消" if summary.get("cancelled") else STATUS.get(summary.get("status"), "未知")
                stamp = datetime.fromtimestamp(path.stat().st_mtime).strftime("%m-%d %H:%M")
                self.batch_result_label.configure(text=f"最近批次 · {stamp} · {status} · 已完成 {completed}/{total}\n"
                    f"截图待审核 {summary.get('screenshotReviewPendingCount', 0)}；执行结果与人工审核分别保存。")
                return
            except (OSError, ValueError, TypeError):
                continue
        self.batch_result_label.configure(text="当前程序和资产目录尚无批量回归结果。")

    def run_batch(self):
        if self.busy or self.recording or self.pending_save or not self.cases:
            return
        app = self.application_var.get().strip()
        if not app or not Path(app).is_file():
            return
        needs_exports = any(item.get("metadata", {}).get("artifacts") for item in self.cases)
        if needs_exports and not self.exports_var.get():
            return
        output = self._playbacks() / ("session_" + datetime.now().strftime("%Y%m%d_%H%M%S_%f") + "_" + uuid.uuid4().hex[:6])
        args = ["--assets", str(self.repository.scenarios.parent), "--journal-tools", str(TOOLS_DIR),
                "--application", app, "--cases", "all", "--output", str(output)]
        if self.exports_var.get():
            args += ["--exports", self.exports_var.get()]
        def operation():
            def progress(event):
                self.events.put(("progress", f"批次 {event['index']}/{event['total']} · {event['case']} · " +
                                 ("开始" if event['phase'] == "begin" else STATUS.get(event.get('status'), event.get('status', '')))))
            try:
                self._app_entry().main(args, session=self._session(), progress=progress)
            except SystemExit as exc:
                raise RuntimeError(f"App 批量入口拒绝当前配置（退出码 {exc.code}）") from exc
            path = output / "summary.json"
            if not path.is_file():
                raise RuntimeError(f"批量执行未产生汇总，请检查：{output}")
            return json.loads(path.read_text(encoding="utf8"))
        self._start_task("batch", f"准备批量回归，共 {len(self.cases)} 个场景…", operation, self._batch_finished)

    def _batch_finished(self, summary):
        self._show_latest_batch()
        status = "已取消" if summary.get("cancelled") or self.cancel_event.is_set() else STATUS.get(summary['status'], summary['status'])
        self.progress_var.set(f"批量回归 {status} · {summary['completedCaseCount']}/{summary['plannedCaseCount']} · 可查看最近批量结果")
        self.log(self.progress_var.get())

    def open_batch_results(self):
        if self.busy or self.recording or self.pending_save or not self.last_batch_output:
            return
        viewer = self.app_root / "tools/review_results.py"
        if not viewer.is_file():
            messagebox.showerror("缺少结果查看器", str(viewer), parent=self.root)
            return
        subprocess.Popen([sys.executable, str(viewer), str(self.last_batch_output)])

    def _session(self):
        return JournalExecutionSession(
            cancel_event=self.cancel_event,
            progress=lambda index, total, entry: self.events.put(("progress", f"{index}/{total} · {entry['kind']}")),
            log=lambda text: self.events.put(("log", text)))

    def run_case(self):
        if self.busy or self.recording or self.pending_save or not self.selected_case:
            return
        selected = self.selected_case
        app = self.application_var.get().strip()
        if selected.get("error") or not app or not Path(app).is_file():
            return
        output = self._playbacks() / ("session_" + datetime.now().strftime("%Y%m%d_%H%M%S_%f") + "_" + uuid.uuid4().hex[:6])
        args = ["--assets", str(self.repository.scenarios.parent), "--journal-tools", str(TOOLS_DIR),
                "--application", app, "--cases", selected["path"].name, "--output", str(output)]
        if selected["metadata"].get("artifacts"):
            if not self.exports_var.get():
                return
            args += ["--exports", self.exports_var.get()]
        def operation():
            try:
                self._app_entry().main(args, session=self._session())
            except SystemExit as exc:
                raise RuntimeError(f"App 验收入口拒绝当前配置（退出码 {exc.code}）") from exc
            path = output / selected["path"].name / "case_result.json"
            if not path.is_file():
                raise RuntimeError(f"验收入口未产生场景结果，请检查：{output}")
            return json.loads((output / "summary.json").read_text(encoding="utf-8"))
        self._start_task("scene", "准备独立测试实例…", operation, self._case_finished)

    def _case_finished(self, result):
        self.progress_var.set("已取消，未完成验收" if self.cancel_event.is_set() else STATUS.get(result["status"], result["status"]))
        self.log("场景结果：" + STATUS.get(result["status"], result["status"]))
        self._show_latest_result()

    def cancel(self):
        if self.busy and self.task in ("scene", "batch", "replay"):
            self.cancel_event.set()
            self.progress_var.set("正在取消；等待当前操作返回并清理测试实例…")
            self._update_controls()

    def toggle_connection_options(self):
        if self.connection_options.winfo_manager():
            self.connection_options.grid_remove()
        else:
            self.connection_options.grid()

    def toggle_connection(self):
        if self.busy:
            return
        if self._connected():
            self.client.disconnect()
            self.client = None
            self.connection_var.set("未连接")
            self._update_controls()
            return
        try:
            port = int(self.port_var.get())
            if not 1 <= port <= 65535:
                raise ValueError("端口需要在 1–65535 之间")
        except ValueError as exc:
            messagebox.showerror("连接设置", str(exc), parent=self.root)
            return
        def operation():
            client = JournalSystem(request_timeout=10)
            try:
                client.connect("127.0.0.1", port)
                client.version()
                paths = client.paths()
                runner = self.runner or JournalRunner(client)
                runner.reconnect(client)
                return client, runner, paths
            except Exception:
                client.disconnect()
                raise
        def finished(result):
            self.client, self.runner, paths = result
            self.repository.set_paths(paths["recordings"], paths["playbacks"])
            self.connection_var.set(f"已连接 · 本机端口 {port}")
            self.progress_var.set("录制连接就绪")
            self.log("录制连接就绪；重连保持原会话与检查点")
            self._show_records()
        self._start_task("connect", "正在连接已有程序…", operation, finished)

    def _show_records(self):
        previous = self.selected_record["path"] if self.selected_record else None
        self.records = [item for item in JournalSessionRepository(recordings=self.repository.recordings).list_sessions() if item["journal_file"]]
        for widget in self.record_list.winfo_children():
            widget.destroy()
        self.record_rows.clear()
        chosen = None
        for index, item in enumerate(self.records):
            self.record_rows[index] = self._list_row(
                self.record_list, item["name"], "已保存的操作录制",
                lambda i=index: self.select_record(i), "record")
            if item["path"] == previous:
                chosen = index
        if chosen is not None:
            self.select_record(chosen)
        elif previous:
            self.new_recording()

    def select_record(self, index):
        if self.busy or self.recording or self.pending_save:
            return
        self.selected_record = self.records[index]
        self.record_heading.configure(text="录制回放检查")
        self._highlight_rows(self.record_rows, index)
        self.saved_name.configure(text=self.selected_record["name"])
        self.record_report = None
        self.record_result.configure(text=("无法回放：" + self.selected_record["error"]) if self.selected_record.get("error") else
                                     "回放启动独立实例；录制检查点是候选数据，不代表正式场景验收。")
        self.editor.grid_remove()
        self.saved_panel.grid(row=3, column=0, sticky="ew")
        self._update_controls()

    def new_recording(self):
        if self.busy or self.recording or self.pending_save:
            return
        self.selected_record = None
        self.record_heading.configure(text="录制新的操作")
        self._highlight_rows(self.record_rows, None)
        self.record_name_var.set("")
        self.saved_panel.grid_remove()
        self.editor.grid(row=3, column=0, sticky="ew")
        self._update_controls()

    def start_recording(self):
        if not self._connected() or self.busy or self.recording or self.pending_save:
            return
        def finished(path):
            self.record_status_var.set("● 录制中 · 悬停和拖动沿用现有录制规则")
            self.progress_var.set("录制中")
            self.log(f"开始录制：{path}")
        self._start_task("record", "正在开始录制…", lambda: self.runner.start_recording(), finished)

    def stop_recording(self):
        if self.busy or not self.runner:
            return
        name = self.record_name_var.get().strip()
        def finished(journal):
            self.record_status_var.set("录制已保存")
            self.progress_var.set("录制已保存，尚未形成正式场景验收")
            self.log(f"已保存：{journal}")
            self._show_records()
            for index, item in enumerate(self.records):
                if item["journal_file"] == journal:
                    self.select_record(index)
                    break
        self._start_task("save", "正在保存录制…",
                         lambda: self.runner.stop_recording({"name": name} if name else {}), finished)

    def checkpoint_dialog(self):
        if not self.recording or self.busy or not self._connected():
            return
        dialog = ctk.CTkToplevel(self.root)
        dialog.title("采集候选检查点")
        dialog.configure(fg_color="#ffffff")
        dialog.transient(self.root)
        dialog.resizable(False, False)
        body = ctk.CTkFrame(dialog, fg_color="#ffffff")
        body.pack(fill=tk.BOTH, expand=True, padx=28, pady=24)
        body.columnconfigure(1, weight=1)
        kind, label, db_id = tk.StringVar(value="截图"), tk.StringVar(), tk.StringVar()
        self._label(body, "采集候选检查点", size=19, bold=True, color="#495872").grid(
            row=0, column=0, columnspan=2, sticky="w", pady=(0, 20))
        self._label(body, "类型", size=12).grid(row=1, column=0, sticky="w")
        db_label = self._label(body, "DBInstanceID", size=12)
        db_entry = ctk.CTkEntry(body, textvariable=db_id, border_width=1, border_color="#e3e8f1",
                               fg_color="#ffffff", font=ctk.CTkFont(self.font_family, 13))
        def changed(_=None):
            if kind.get() == "DB 对象数据":
                db_label.grid(row=2, column=0, sticky="w", pady=10)
                db_entry.grid(row=2, column=1, padx=(16, 0), pady=10, sticky="ew")
            else:
                db_label.grid_remove()
                db_entry.grid_remove()
        selector = ctk.CTkOptionMenu(body, variable=kind, values=("截图", "DB 对象数据"), command=changed,
                                     fg_color="#f3f5fc", button_color="#e8edf7", button_hover_color="#dce3f1",
                                     text_color="#77869e", dropdown_fg_color="#ffffff",
                                     dropdown_text_color="#77869e", dropdown_hover_color="#edf0fc",
                                     font=ctk.CTkFont(self.font_family, 13))
        selector.grid(row=1, column=1, padx=(16, 0), pady=6, sticky="ew")
        self._label(body, "备注", size=12).grid(row=3, column=0, sticky="w")
        ctk.CTkEntry(body, textvariable=label, border_width=1, border_color="#e3e8f1",
                    fg_color="#ffffff", font=ctk.CTkFont(self.font_family, 13)).grid(
            row=3, column=1, padx=(16, 0), pady=10, sticky="ew")
        self._label(body, "采集候选数据；需核对后纳入标准资产，不能自动批准为基线。",
                     wraplength=380, justify="left", size=11, color="#a0adbf").grid(
            row=4, column=0, columnspan=2, sticky="w", pady=17)
        buttons = ctk.CTkFrame(body, fg_color="transparent")
        buttons.grid(row=5, column=0, columnspan=2, sticky="e", pady=(8, 0))
        self._button(buttons, "取消", dialog.destroy, kind="quiet").pack(side=tk.LEFT, padx=8)
        def capture():
            mode, identifier, text = kind.get(), db_id.get().strip(), label.get().strip()
            if mode == "DB 对象数据":
                try:
                    identifier = JournalSystem.normalize_db_id(identifier)
                except ValueError as exc:
                    messagebox.showerror("对象 ID", str(exc), parent=dialog)
                    return
            dialog.destroy()
            operation = (lambda: self.runner.record_db(identifier, text)) if mode == "DB 对象数据" else (
                lambda: self.runner.record_screenshot(text or "checkpoint"))
            self._start_task("checkpoint", "正在采集候选检查点…", operation,
                             lambda entry: self.log(f"候选检查点已采集：{entry['kind']}；尚未批准为标准基线"))
        self._button(buttons, "采集", capture, kind="primary").pack(side=tk.LEFT)
        dialog.wait_visibility()
        dialog.grab_set()

    def replay_record(self):
        if self.busy or self.recording or self.pending_save or not self.selected_record or self.selected_record.get("error"):
            return
        app = self.application_var.get().strip()
        if not app or not Path(app).is_file():
            return
        journal = self.selected_record["journal_file"]
        def finished(result):
            self.record_report = Path(result["reportPath"])
            status = STATUS.get(result.get("acceptance"), "执行完成")
            self.record_result.configure(text=f"诊断回放：{status}。候选检查点不代表正式场景标准已获确认。")
            self.progress_var.set("录制诊断回放已结束")
            self.log(f"录制诊断报告：{self.record_report}")
        self._start_task("replay", "准备独立实例回放录制…",
                         lambda: self._session().run(journal, application=app, startup_timeout=120), finished)

    @staticmethod
    def _open_path(path):
        command = "open" if sys.platform == "darwin" else "explorer" if sys.platform.startswith("win") else "xdg-open"
        subprocess.Popen([command, str(Path(path).resolve())])

    def open_report(self):
        if self.last_report and self.last_report.is_file():
            webbrowser.open(self.last_report.resolve().as_uri())

    def open_record_report(self):
        if self.record_report and self.record_report.is_file():
            webbrowser.open(self.record_report.resolve().as_uri())

    def open_result_folder(self):
        if self.last_output:
            self._open_path(self.last_output)

    def open_record_folder(self):
        path = self.selected_record["path"] if self.selected_record else self.repository.recordings
        if path and Path(path).is_dir():
            self._open_path(path)

    def close(self):
        if self.busy:
            if not self._close_requested and not messagebox.askyesno(
                    "操作进行中", "取消当前操作并在清理结束后关闭？", parent=self.root):
                return
            self._close_requested = True
            self.cancel_event.set()
            self.progress_var.set("正在等待当前操作返回并清理，请稍候…")
            return
        if self.recording or self.pending_save:
            if not messagebox.askyesno("录制尚未保存",
                    "本地检查点或待保存内容尚未保存，关闭不会停止 App 中的录制。\n建议先重连并保存。仍要关闭吗？",
                    default="no", parent=self.root):
                self._close_requested = False
                return
        if self.client:
            self.client.disconnect()
        self._closed = True
        self.root.after_cancel(self._event_timer)
        self.root.after_cancel(self._load_timer)
        self.root.destroy()


def main():
    parser = argparse.ArgumentParser(description="Journal 场景测试与录制工具")
    parser.add_argument("--port", type=int, default=9999)
    parser.add_argument("--application", type=Path, help="本次要验证的实际可执行文件")
    parser.add_argument("--app-root", type=Path, help="提供统一验收入口的 App 源码目录")
    args = parser.parse_args()
    root = ctk.CTk()
    JournalSystemGUI(root, port=args.port, application=args.application, app_root=args.app_root)
    root.mainloop()


if __name__ == "__main__":
    main()
