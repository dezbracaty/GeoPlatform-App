#!/usr/bin/env python3
"""Native viewer for App batch-regression evidence and independent human reviews.

Usage: python tools/review_results.py [batch-directory]
This window consumes ReviewBatch; it never runs Journal verification or changes baselines.
"""
from __future__ import annotations

import argparse
from collections import OrderedDict
import getpass
import math
from pathlib import Path
import sys
import time

try:
    import tkinter as tk
    from tkinter import filedialog, messagebox
    import customtkinter as ctk
    from PIL import Image, ImageDraw, ImageTk
except ImportError as exc:
    if __name__ != "__main__":
        raise
    print("无法启动结果查看窗口：" + str(exc) + "\n"
          "请使用带 Tk 支持的 Python，并在该解释器中安装依赖：\n"
          f"  {sys.executable} -m pip install customtkinter Pillow\n"
          "若缺少 _tkinter，请安装对应 Python 的 Tk 支持（例如 python-tk）。", file=sys.stderr)
    raise SystemExit(2)

from review_model import ReviewBatch

COLORS = {
    "page": ("#fcfdff", "#191d23"), "stage": ("#edf1f6", "#12161c"),
    "sheet": ("#ffffff", "#242a33"), "text": ("#253042", "#e8eef7"),
    "muted": ("#637085", "#a9b4c5"), "line": ("#dde4ed", "#384251"),
    "blue": ("#0878c5", "#79c8ff"), "active": ("#eaf4fd", "#233c50"),
    "green": ("#277448", "#8dd9ac"), "orange": ("#8e6114", "#ebbe7b"),
    "red": ("#b23640", "#ff999f"),
}
DECISIONS = {"pending": "待人工审核", "accepted": "差异可接受", "defect": "确认缺陷"}
EXECUTION = {"completed": "执行完成", "captured": "已采集", "passed": "执行通过",
             "failed": "执行失败", "blocked": "执行阻塞", "not_run": "未执行",
             "capture_error": "采集失败", "pending": "比较中"}


class ImageView(ctk.CTkFrame):
    """A clipped image viewport; the owner shares normalized pan/zoom across all three."""

    def __init__(self, parent, title, owner):
        super().__init__(parent, fg_color="transparent", corner_radius=0)
        self.owner, self.source, self.photo = owner, None, None
        self.zoom, self.center, self.drag = 1., (.5, .5), None
        self.title = owner.label(self, title, size=12)
        self.title.pack(fill="x", pady=(0, 6))
        self.canvas = tk.Canvas(self, height=225, highlightthickness=1,
                                background=owner.color("sheet"), highlightbackground=owner.color("line"))
        self.canvas.pack(fill="both", expand=True)
        self.canvas.bind("<Configure>", lambda _: self.draw())
        self.canvas.bind("<ButtonPress-1>", self.press)
        self.canvas.bind("<B1-Motion>", self.move)
        self.canvas.bind("<ButtonRelease-1>", lambda _: setattr(self, "drag", None))
        self.canvas.bind("<MouseWheel>", self.wheel)
        self.canvas.bind("<Button-4>", self.wheel)
        self.canvas.bind("<Button-5>", self.wheel)
        self.message = "尚无截图"

    def set_image(self, source, message="尚无截图"):
        self.source, self.message = source, message
        self.draw()

    def set_view(self, zoom, center):
        self.zoom, self.center = zoom, center
        self.draw()

    def scale(self):
        if self.source is None:
            return 1.
        width, height = max(1, self.canvas.winfo_width() - 12), max(1, self.canvas.winfo_height() - 12)
        return min(width / self.source.width, height / self.source.height) * self.zoom

    def draw(self):
        canvas = self.canvas
        canvas.configure(background=self.owner.color("sheet"), highlightbackground=self.owner.color("line"))
        canvas.delete("all")
        self.photo = None
        width, height = canvas.winfo_width(), canvas.winfo_height()
        if width < 3 or height < 3:
            return
        if self.source is None:
            canvas.create_text(width / 2, height / 2, text=self.message, fill=self.owner.color("muted"),
                               width=max(60, width - 24), justify="center", font=(self.owner.font_family, 12))
            return
        scale = self.scale()
        rendered_width, rendered_height = self.source.width * scale, self.source.height * scale
        center_x, center_y = self.center
        center_x = .5 if rendered_width <= width else min(1 - width / rendered_width / 2, max(width / rendered_width / 2, center_x))
        center_y = .5 if rendered_height <= height else min(1 - height / rendered_height / 2, max(height / rendered_height / 2, center_y))
        origin_x, origin_y = width / 2 - center_x * rendered_width, height / 2 - center_y * rendered_height
        # Crop before resizing: zooming a large original never allocates a huge offscreen bitmap.
        left, top = max(0, int(-origin_x / scale)), max(0, int(-origin_y / scale))
        right = min(self.source.width, math.ceil((width - origin_x) / scale))
        bottom = min(self.source.height, math.ceil((height - origin_y) / scale))
        if right <= left or bottom <= top:
            return
        crop = self.source.crop((left, top, right, bottom))
        crop = crop.resize((max(1, round(crop.width * scale)), max(1, round(crop.height * scale))), Image.Resampling.LANCZOS)
        self.photo = ImageTk.PhotoImage(crop, master=canvas)
        canvas.create_image(origin_x + left * scale, origin_y + top * scale, image=self.photo, anchor="nw")

    def press(self, event):
        self.drag = (event.x, event.y)
        self.canvas.focus_set()

    def move(self, event):
        if self.drag is None or self.source is None:
            return
        scale = self.scale()
        dx, dy = event.x - self.drag[0], event.y - self.drag[1]
        self.drag = (event.x, event.y)
        self.owner.set_pan(self.center[0] - dx / (self.source.width * scale),
                           self.center[1] - dy / (self.source.height * scale))

    def wheel(self, event):
        direction = self.owner.wheel_direction(event)
        if not direction:
            return "break"
        # Detail wheel never selects another checkpoint. Ctrl / Cmd zooms; plain wheel pans.
        if event.state & (0x4 | (0x8 if sys.platform == "darwin" else 0)):
            self.owner.set_zoom(self.zoom * (1.15 if direction < 0 else 1 / 1.15))
        elif self.zoom <= 1:
            return None  # At fit scale, let the ordinary detail document scroll.
        elif self.source is not None:
            self.owner.set_pan(self.center[0], self.center[1] + direction * .07 / self.zoom)
        return "break"


class ReviewWindow(ctk.CTk):
    """One native window: checkpoint reel above, evidence and review below."""

    def __init__(self, directory=None):
        super().__init__()
        self.title("GPlatform · 批量回归结果")
        self.geometry("1180x940")
        self.minsize(900, 680)
        self.font_family = "PingFang SC" if sys.platform == "darwin" else (
            "Microsoft YaHei UI" if sys.platform.startswith("win") else "DejaVu Sans")
        self.batch, self.selected = None, 0
        self.detail_open, self.hovered, self.raw_diff = False, False, False
        self.zoom, self.pan = 1., (.5, .5)
        self._images, self._image_errors, self._photos = OrderedDict(), {}, []
        self._card_cache = OrderedDict()
        self._animation_timer, self._note_timer = None, None
        self._animation_offset, self._animation_started = 0., 0.
        self._last_wheel, self._last_direction = -float("inf"), 0
        self._note_dirty, self._editor_row_id, self._closed = False, None, False
        self.reviewer = tk.StringVar(value=getpass.getuser())
        self.position = tk.StringVar(value="0 / 0")
        self.jump = tk.StringVar()
        self.summary = tk.StringVar(value="尚未打开批次")
        self._build()
        self.protocol("WM_DELETE_WINDOW", self.close)
        self.bind("<MouseWheel>", self.on_wheel, add="+")
        self.bind("<Button-4>", self.on_wheel, add="+")
        self.bind("<Button-5>", self.on_wheel, add="+")
        self.bind("<Up>", lambda e: self.key_step(e, -1), add="+")
        self.bind("<Down>", lambda e: self.key_step(e, 1), add="+")
        self.bind("<Escape>", self.escape, add="+")
        self.bind("<F5>", lambda _: self.reload_batch())
        if directory is not None:
            self.after(0, lambda: self.open_batch(directory))
        else:
            self.after(0, self.choose_batch)

    def color(self, key):
        return COLORS[key][1 if self._get_appearance_mode() == "dark" else 0]

    def label(self, parent, text="", *, size=13, color="muted", **kwargs):
        options = dict(text=text, text_color=COLORS[color], anchor="w",
                       font=ctk.CTkFont(self.font_family, size))
        options.update(kwargs)
        return ctk.CTkLabel(parent, **options)

    def button(self, parent, text, command, **kwargs):
        options = dict(text=text, command=command, height=32, width=88,
                       corner_radius=6, border_width=1, border_color=COLORS["line"],
                       fg_color=COLORS["sheet"], hover_color=COLORS["active"],
                       text_color=COLORS["text"], font=ctk.CTkFont(self.font_family, 12))
        options.update(kwargs)
        return ctk.CTkButton(parent, **options)

    def _build(self):
        self.configure(fg_color=COLORS["page"])
        self.columnconfigure(0, weight=1)
        self.rowconfigure(2, weight=1)
        header = ctk.CTkFrame(self, fg_color="transparent", corner_radius=0)
        header.grid(row=0, column=0, sticky="ew", padx=22, pady=(16, 10))
        header.columnconfigure(0, weight=1)
        self.label(header, "GPlatform / Journal 验收结果", size=12).grid(row=0, column=0, sticky="w")
        self.label(header, "截图对照", size=22, color="text").grid(row=1, column=0, sticky="w")
        self.label(header, textvariable=self.summary, size=12).grid(row=0, column=1, rowspan=2, padx=18)
        self.button(header, "打开结果", self.choose_batch).grid(row=0, column=2, rowspan=2)
        self.button(header, "刷新", self.reload_batch, width=52).grid(row=0, column=3, rowspan=2, padx=(8, 0))
        navigation = ctk.CTkFrame(self, fg_color="transparent", corner_radius=0)
        navigation.grid(row=1, column=0, sticky="ew", padx=22, pady=(4, 12))
        navigation.columnconfigure(0, weight=1)
        names = ctk.CTkFrame(navigation, fg_color="transparent")
        names.grid(row=0, column=0, sticky="ew")
        self.title_label = self.label(names, "选择一个批量回归结果目录", size=16, color="text")
        self.title_label.pack(fill="x")
        self.path_label = self.label(names, "读取批次或单次回放结果；不会改写执行结果或标准图", size=12)
        self.path_label.pack(fill="x")
        self.jump_menu = ctk.CTkOptionMenu(navigation, variable=self.jump, values=["尚无检查点"], command=self.jump_to,
                                         width=220, height=32, font=ctk.CTkFont(self.font_family, 12),
                                         fg_color=COLORS["sheet"], button_color=COLORS["stage"],
                                         button_hover_color=COLORS["active"], text_color=COLORS["text"], state="disabled")
        self.jump_menu.grid(row=0, column=1, padx=12)
        self.prev_button = self.button(navigation, "↑", lambda: self.select(self.selected - 1), width=34)
        self.prev_button.grid(row=0, column=2)
        self.label(navigation, textvariable=self.position, size=12, width=48, anchor="center").grid(row=0, column=3, padx=5)
        self.next_button = self.button(navigation, "↓", lambda: self.select(self.selected + 1), width=34)
        self.next_button.grid(row=0, column=4)
        self.body = ctk.CTkScrollableFrame(self, fg_color=COLORS["page"], corner_radius=0)
        self.body.grid(row=2, column=0, sticky="nsew")
        self.body.columnconfigure(0, weight=1)
        self.browser = ctk.CTkFrame(self.body, fg_color="transparent", corner_radius=0)
        self.browser.grid(row=0, column=0, sticky="ew")
        self.browser.columnconfigure(0, weight=1)
        self.stage = tk.Canvas(self.browser, height=370, background=self.color("stage"), highlightthickness=0)
        self.stage.grid(row=0, column=0, sticky="ew")
        self.stage.bind("<Configure>", lambda _: self.draw_stage())
        self.stage.bind("<Motion>", self.stage_motion)
        self.stage.bind("<Leave>", self.stage_leave)
        self.stage.bind("<Button-1>", self.stage_click)
        status = ctk.CTkFrame(self.browser, fg_color="transparent")
        status.grid(row=1, column=0, sticky="ew", padx=18, pady=10)
        status.columnconfigure(1, weight=1)
        self.execution_label = self.label(status, "", size=12)
        self.execution_label.grid(row=0, column=0, sticky="w")
        self.review_label = self.label(status, "", size=12)
        self.review_label.grid(row=0, column=1, sticky="w", padx=16)
        self.label(status, "滚轮上下切组 · 悬停展开标准/差异 · 点击展开下方详情", size=11).grid(row=0, column=2)
        self.detail = ctk.CTkFrame(self.body, fg_color=COLORS["page"], corner_radius=0,
                                   border_width=1, border_color=COLORS["line"])
        self.detail.grid(row=1, column=0, sticky="ew", pady=(0, 12))
        self.detail.columnconfigure(0, weight=1)
        self._build_details()
        self.detail.grid_remove()
        self.auto_results = ctk.CTkFrame(self.body, fg_color="transparent")
        self.auto_results.grid(row=2, column=0, sticky="ew", padx=18, pady=18)
        self.label(self.auto_results, "场景自动执行结果（无截图检查点）", size=15, color="text").pack(fill="x", pady=(0, 10))
        self.auto_text = ctk.CTkTextbox(self.auto_results, height=300, fg_color=COLORS["stage"],
                                       font=ctk.CTkFont(self.font_family, 13), wrap="word")
        self.auto_text.pack(fill="x")
        self.auto_results.grid_remove()
        self.footer = self.label(self, "截图提示与人工审核独立于执行结果；颜色不代表缺陷或通过结论", size=11)
        self.footer.grid(row=3, column=0, sticky="ew", padx=22, pady=8)
        self._update_status()

    def _build_details(self):
        tools = ctk.CTkFrame(self.detail, fg_color="transparent")
        tools.grid(row=0, column=0, sticky="ew", padx=16, pady=12)
        tools.columnconfigure(0, weight=1)
        self.detail_title = self.label(tools, "检查点详情", size=14, color="text")
        self.detail_title.grid(row=0, column=0, sticky="w")
        self.button(tools, "收起详情", self.hide_details).grid(row=0, column=1, padx=(8, 0))
        display = ctk.CTkFrame(self.detail, fg_color="transparent")
        display.grid(row=1, column=0, sticky="ew", padx=16, pady=(0, 10))
        self.diff_mode = ctk.CTkSegmentedButton(display, values=["降噪提示", "原始像素差异"], command=self.set_diff_mode,
                                                font=ctk.CTkFont(self.font_family, 12))
        self.diff_mode.set("降噪提示")
        self.diff_mode.pack(side="left")
        self.button(display, "适应宽度", lambda: self.set_zoom(1)).pack(side="left", padx=12)
        self.zoom_slider = ctk.CTkSlider(display, from_=1, to=4, number_of_steps=30, command=self.set_zoom, width=150)
        self.zoom_slider.set(1)
        self.zoom_slider.pack(side="left", padx=8)
        self.zoom_label = self.label(display, "100%", size=12, width=42)
        self.zoom_label.pack(side="left")
        self.label(display, "拖动平移 · Ctrl/⌘ + 滚轮缩放", size=11).pack(side="right")
        self.legend = self.label(self.detail, "红：需重点核对的差异   橙：疑似渲染差异或弱提示，仍需人工确认", size=12)
        self.legend.grid(row=2, column=0, sticky="ew", padx=16)
        self.metrics = self.label(self.detail, "", size=11, wraplength=1080)
        self.metrics.grid(row=3, column=0, sticky="ew", padx=16, pady=(3, 8))
        images = ctk.CTkFrame(self.detail, fg_color=COLORS["stage"], corner_radius=0)
        images.grid(row=4, column=0, sticky="ew")
        self.views = []
        for column, title in enumerate(("标准截图", "本次截图", "差异提示（标准图底图）")):
            images.columnconfigure(column, weight=1, uniform="images")
            view = ImageView(images, title, self)
            view.grid(row=0, column=column, sticky="nsew", padx=(12 if not column else 6, 12 if column == 2 else 6), pady=12)
            self.views.append(view)
        inspector = ctk.CTkFrame(self.detail, fg_color="transparent")
        inspector.grid(row=5, column=0, sticky="ew", padx=16, pady=16)
        inspector.columnconfigure(0, weight=1)
        inspector.columnconfigure(1, weight=1)
        info = ctk.CTkFrame(inspector, fg_color="transparent")
        info.grid(row=0, column=0, sticky="new", padx=(0, 16))
        self.label(info, "证据与审核", size=14, color="text").pack(fill="x")
        self.detail_execution = self.label(info, "", size=12)
        self.detail_execution.pack(fill="x", pady=(10, 4))
        self.hint = self.label(info, "", size=12, wraplength=490, justify="left")
        self.hint.pack(fill="x", pady=4)
        self.saved_review = self.label(info, "", size=11, wraplength=490)
        self.saved_review.pack(fill="x", pady=4)
        editor = ctk.CTkFrame(inspector, fg_color="transparent")
        self.review_editor = editor
        editor.grid(row=0, column=1, sticky="new")
        identity = ctk.CTkFrame(editor, fg_color="transparent")
        identity.pack(fill="x", pady=(0, 6))
        self.label(identity, "审核人", size=12).pack(side="left", padx=(0, 8))
        self.reviewer_entry = ctk.CTkEntry(identity, textvariable=self.reviewer, height=30)
        self.reviewer_entry.pack(side="left", fill="x", expand=True)
        self.note = ctk.CTkTextbox(editor, height=72, border_width=1, border_color=COLORS["line"],
                                   fg_color=COLORS["sheet"], font=ctk.CTkFont(self.font_family, 12), wrap="word")
        self.note.pack(fill="x")
        self.note.bind("<<Modified>>", self.note_modified, add="+")
        actions = ctk.CTkFrame(editor, fg_color="transparent")
        actions.pack(fill="x", pady=(8, 4))
        self.review_buttons = {}
        for key, label in (("accepted", "差异可接受"), ("defect", "确认缺陷"), ("pending", "暂不判断")):
            button = self.button(actions, label, lambda value=key: self.save_review(value))
            button.pack(side="left", padx=(0, 8))
            self.review_buttons[key] = button
        self.save_status = self.label(editor, "备注自动保存；审核不改写执行结果，不替换标准图", size=11, wraplength=500)
        self.save_status.pack(fill="x")

    def choose_batch(self):
        if not self.flush_notes():
            return
        directory = filedialog.askdirectory(parent=self, title="选择批次或单次回放结果目录")
        if directory:
            self.open_batch(directory)

    def open_batch(self, directory):
        if not self.flush_notes():
            return False
        try:
            batch = ReviewBatch(Path(directory))
        except (OSError, ValueError, TypeError) as exc:
            messagebox.showerror("无法打开批次", str(exc), parent=self)
            return False
        self.batch, self.selected = batch, 0
        self.title(f"GPlatform · 批量回归结果 · {batch.document.get('status', 'unknown')} · {batch.directory.name}")
        if batch.rows:
            self.browser.grid()
            self.auto_results.grid_remove()
        else:
            self.browser.grid_remove()
            self.auto_results.grid()
            cases = batch.document.get("cases", [])
            if isinstance(cases, dict):
                cases = [dict(value, id=name) if isinstance(value, dict) else {"id": name, "status": value}
                         for name, value in cases.items()]
            self.auto_text.configure(state="normal")
            self.auto_text.delete("1.0", "end")
            self.auto_text.insert("1.0", "\n\n".join(
                f"{case.get('id', case.get('name', '未命名场景'))}\n自动结果：{case.get('status', 'unknown')}" +
                (f"\n{case['error']}" if case.get("error") else "") for case in cases) or "本批次未记录场景执行结果")
            self.auto_text.configure(state="disabled")
        self.detail_open, self.hovered = False, False
        self._editor_row_id, self._note_dirty = None, False
        self._images.clear()
        self._image_errors.clear()
        self._photos.clear()
        self._card_cache.clear()
        self._jump_values = [f"{i + 1} · {row.get('case', '')} / #{row.get('index', '')} · {row.get('label', '')}"
                             for i, row in enumerate(batch.rows)]
        self.jump_menu.configure(values=self._jump_values or ["没有截图检查点"],
                                 state="normal" if batch.rows else "disabled")
        self.detail.grid_remove()
        self._render_selection()
        return True

    def reload_batch(self):
        if self.batch is None or not self.flush_notes():
            return
        directory = self.batch.directory
        row_id = self.current().get("id") if self.current() else None
        if self.open_batch(directory):
            index = next((i for i, row in enumerate(self.batch.rows) if row.get("id") == row_id), 0)
            self.select(index)

    def current(self):
        return self.batch.rows[self.selected] if self.batch and self.batch.rows else None

    def image_for(self, row, *keys):
        if not self.batch or row is None:
            return None
        for key in keys:
            try:
                path = self.batch.path(row, key)
            except (OSError, ValueError) as exc:
                self._image_errors[(row.get("id"), key)] = str(exc)
                continue
            if path is None:
                continue
            cache_key = str(path)
            if cache_key in self._images:
                self._images.move_to_end(cache_key)
                return self._images[cache_key]
            try:
                with Image.open(path) as opened:
                    image = opened.convert("RGB")
                self._images[cache_key] = image
                while len(self._images) > 9:
                    self._images.popitem(last=False)
                return image
            except (OSError, ValueError) as exc:
                self._image_errors[(row.get("id"), key)] = str(exc)
        return None

    def _render_selection(self):
        row = self.current()
        if row is None:
            self.title_label.configure(text="此批次没有截图检查点")
            self.path_label.configure(text="自动执行/DB结果保留在本批次 summary.json 中")
            self.position.set("0 / 0")
        else:
            self.title_label.configure(text=row.get("label") or f"截图检查点 #{row.get('index', '')}")
            self.path_label.configure(text=f"{row.get('case', '')} / 检查点 #{row.get('index', '')}")
            self.position.set(f"{self.selected + 1} / {len(self.batch.rows)}")
            self.jump.set(self._jump_values[self.selected])
        self.prev_button.configure(state="normal" if row and self.selected > 0 else "disabled")
        self.next_button.configure(state="normal" if row and self.selected < len(self.batch.rows) - 1 else "disabled")
        self._update_status()
        self.draw_stage()
        if self.detail_open:
            self.render_details()

    def _update_status(self):
        row = self.current()
        if self.batch:
            pending = sum(self.batch.can_review(r) and self.batch.decision(r).get("decision", "pending") == "pending"
                          for r in self.batch.rows)
            counts = {status: sum(r.get('automaticStatus', r.get('executionStatus')) == status for r in self.batch.rows)
                      for status in ('passed', 'failed', 'pending')}
            self.summary.set(f"{len(self.batch.rows)} 张 · 通过 {counts['passed']} · 失败 {counts['failed']} · "
                             f"比较中 {counts['pending']} · 结果 {self.batch.document.get('status', '未知')}" +
                             (f" · 待人工审核 {pending}" if pending else ''))
            if self.batch.document.get('screenshotReviewError'):
                self.summary.set(self.summary.get() + " · 辅助清单更新失败，当前对照可能不完整")
        if row is None:
            self.execution_label.configure(text="")
            self.review_label.configure(text="")
            return
        execution = row.get("executionStatus", "unknown")
        exact = (row.get('comparisonMetadata') or {}).get('policyVersion') == 'journal-exact'
        self.execution_label.configure(text=EXECUTION.get(execution, execution),
                                       text_color=COLORS["green"] if execution in ("completed", "captured", "passed") else COLORS["red"])
        can_review = self.batch.can_review(row)
        decision = self.batch.decision(row).get("decision", "pending")
        unavailable = "诊断截图 · 不作标准审核" if row.get("kind") != "verify.screenshot" else (
            "未执行 · 不可审核" if execution == "not_run" else "证据或比较不可用 · 不可审核")
        self.review_label.configure(text=DECISIONS.get(decision, decision) if can_review else unavailable,
                                   text_color=COLORS["green" if decision == "accepted" else "red" if decision == "defect" or not can_review else "orange"])
        if exact:
            status = row.get('automaticStatus', execution)
            count = (row.get('comparisonMetadata') or {}).get('rawDifferencePixelCount')
            self.execution_label.configure(text={'passed': '截图校验通过', 'failed': '截图校验失败',
                                                 'pending': '后台比较中'}.get(status, EXECUTION.get(status, status)))
            self.review_label.configure(text=f'整图 1:1 · 差异像素 {count}' if count is not None else
                                        '整图 1:1 · ' + ('等待比较完成' if status == 'pending' else '标准缺失或比较异常'),
                                        text_color=COLORS['green' if status == 'passed' else 'orange' if status == 'pending' else 'red'])
            self.footer.configure(text='自动校验结果直接读取运行记录；此窗口不改变通过状态或标准图')
        else:
            self.footer.configure(text='截图提示与人工审核独立于执行结果；颜色不代表缺陷或通过结论')

    def select(self, index):
        if not self.batch or not 0 <= index < len(self.batch.rows) or index == self.selected:
            return
        if not self.flush_notes():
            return
        direction = 1 if index > self.selected else -1
        self.selected, self.zoom, self.pan = index, 1., (.5, .5)
        self.zoom_slider.set(1)
        if self._animation_timer is not None:
            self.after_cancel(self._animation_timer)
        self._animation_offset = direction * 180.
        self._animation_started = time.monotonic()
        self._render_selection()
        self._animate()

    def jump_to(self, value):
        try:
            self.select(self._jump_values.index(value))
        except ValueError:
            pass

    @staticmethod
    def wheel_direction(event):
        number = getattr(event, "num", None)
        if number == 4:
            return -1
        if number == 5:
            return 1
        delta = getattr(event, "delta", 0)
        return -1 if delta > 0 else 1 if delta < 0 else 0

    def on_wheel(self, event):
        # Native Toplevel binding runs before CTkScrollableFrame's global scroll binding.
        if self.in_widget(event.widget, self.detail) or isinstance(event.widget, (tk.Entry, tk.Text, tk.Listbox)):
            return None
        direction = self.wheel_direction(event)
        if not direction or not self.batch:
            return None
        now = time.monotonic()
        if direction != self._last_direction or now - self._last_wheel >= .24:
            self._last_wheel, self._last_direction = now, direction
            self.select(self.selected + direction)
        return "break"

    @staticmethod
    def in_widget(widget, ancestor):
        while widget is not None:
            if widget is ancestor:
                return True
            widget = getattr(widget, "master", None)
        return False

    def key_step(self, event, direction):
        if self.in_widget(event.widget, self.detail) or isinstance(event.widget, (tk.Entry, tk.Text, tk.Listbox)):
            return None
        self.select(self.selected + direction)
        return "break"

    def escape(self, _event=None):
        if self.detail_open:
            self.hide_details()
            return "break"
        return None

    def _animate(self):
        elapsed = (time.monotonic() - self._animation_started) / .22
        if elapsed >= 1:
            self._animation_offset, self._animation_timer = 0., None
            self.draw_stage()
            return
        self._animation_offset = math.copysign(180 * (1 - elapsed) ** 3, self._animation_offset)
        self.draw_stage()
        self._animation_timer = self.after(16, self._animate)

    def stage_motion(self, event):
        width = self.stage.winfo_width()
        card_width = min(355, max(160, (width - 68) / 2))
        card_height = card_width / 1.4486 + 45
        y = self.stage.winfo_height() / 2
        inside = abs(event.x - width / 2) <= card_width + 18 and abs(event.y - y) <= card_height / 2 + 15
        if inside != self.hovered:
            self.hovered = inside
            self.draw_stage()

    def stage_leave(self, _event):
        if self.hovered:
            self.hovered = False
            self.draw_stage()

    def stage_click(self, event):
        if not self.current():
            return
        card_width = min(355, max(160, (self.stage.winfo_width() - 68) / 2))
        for low, high, index in getattr(self, "_stage_slots", []):
            x_limit = card_width + 18 if index == self.selected else card_width * .72 / 2 + 22
            if low <= event.y <= high and abs(event.x - self.stage.winfo_width() / 2) <= x_limit:
                if index != self.selected:
                    self.select(index)
                else:
                    self.show_details()
                return

    def draw_stage(self):
        canvas = self.stage
        canvas.configure(background=self.color("stage"))
        canvas.delete("all")
        self._photos = []
        width, height = canvas.winfo_width(), canvas.winfo_height()
        if not self.current():
            canvas.create_text(width / 2, max(90, height / 2), text="打开批次目录，查看截图与差异证据",
                               fill=self.color("muted"), font=(self.font_family, 14))
            return
        card_width = min(355, max(160, (width - 68) / 2))
        card_height = card_width / 1.4486 + 45
        center_y = height / 2
        self._stage_slots = []
        # Neighbors recede above/below the current pair; the current group is painted last.
        for offset in (-1, 1, 0):
            index = self.selected + offset
            if not 0 <= index < len(self.batch.rows):
                continue
            row = self.batch.rows[index]
            scale = 1. if offset == 0 else .72
            y = center_y + offset * 200 + self._animation_offset
            self._stage_slots.append((y - card_height * scale / 2, y + card_height * scale / 2, index))
            spread = (card_width / 2 + 10) if offset == 0 and self.hovered else 12
            before = self.image_for(row, "expectedView", "expected")
            after = self.image_for(row, "rawDiff" if self.raw_diff else "diff")
            exact = (row.get('comparisonMetadata') or {}).get('policyVersion') == 'journal-exact'
            for side, source, label in ((-1, before, "标准图"), (1, after, "整图逐像素差异" if exact else "原始像素差异" if self.raw_diff else "差异提示 · 降噪")):
                x = width / 2 + side * spread * scale
                angle = 0 if offset == 0 and self.hovered else (-3 if side < 0 else 3)
                photo = self._card_image(source, row, label, round(card_width * scale), round(card_height * scale), angle,
                                         dimmed=offset != 0)
                self._photos.append(photo)
                canvas.create_image(x, y, image=photo)
                w, h = card_width * scale, card_height * scale
                ink = self.color("muted") if offset else self.color("blue" if side > 0 else "muted")
                canvas.create_text(x - w / 2 + 10, y - h / 2 + 10, text=label, anchor="nw",
                                   fill=ink, font=(self.font_family, 10))
                canvas.create_text(x + w / 2 - 10, y - h / 2 + 10, text=f"#{row.get('index', '')}",
                                   anchor="ne", fill=ink, font=(self.font_family, 10))
                name = str(row.get("label", ""))
                max_chars = max(10, round(w / 13))
                if len(name) > max_chars:
                    name = name[:max_chars - 1] + "…"
                canvas.create_text(x - w / 2 + 10, y + h / 2 - 8, text=name, anchor="sw", fill=ink,
                                   font=(self.font_family, 10))
                if source is None:
                    canvas.create_text(x, y, text="缺少标准截图" if side < 0 else "缺少差异图",
                                       fill=ink, font=(self.font_family, 12), width=max(60, w - 20))
        # Check the visually foremost group first when hit areas overlap.
        self._stage_slots.sort(key=lambda entry: entry[2] != self.selected)

    def _card_image(self, source, row, label, width, height, angle, *, dimmed):
        key = (row.get("id"), label, width, height, angle, dimmed, self.color("sheet"), id(source))
        if key in self._card_cache:
            self._card_cache.move_to_end(key)
            return self._card_cache[key]
        image = Image.new("RGBA", (width + 28, height + 28))
        draw = ImageDraw.Draw(image)
        draw.rounded_rectangle((14, 14, width + 14, height + 14), radius=9, fill=self.color("sheet"), outline=self.color("line"))
        if source is not None:
            box = source.copy()
            box.thumbnail((max(1, width - 16), max(1, height - 42)), Image.Resampling.LANCZOS)
            image.paste(box, (14 + (width - box.width) // 2, 36 + (height - 42 - box.height) // 2))
        # Tk renders labels with the platform font: no bundled font or bitmap text substitution.
        photo_image = image.rotate(angle, resample=Image.Resampling.BICUBIC, expand=False)
        if dimmed:
            backdrop = Image.new("RGBA", photo_image.size, self.color("stage"))
            photo_image = Image.blend(backdrop, photo_image, .62)
        photo = ImageTk.PhotoImage(photo_image, master=self.stage)
        self._card_cache[key] = photo
        while len(self._card_cache) > 48:
            self._card_cache.popitem(last=False)
        return photo

    def show_details(self):
        if self.current() is None:
            return
        self.detail_open = True
        self.detail.grid()
        self.render_details()
        # The reel stays in the same scroll document; no second page or new window.
        self.after_idle(self._reveal_details)

    def _reveal_details(self):
        if not self.detail_open or not self.winfo_exists():
            return
        self.update_idletasks()
        canvas = self.body._parent_canvas
        region = canvas.bbox("all")
        if region and region[3] > 0:
            top = self.detail.winfo_y()
            visible_top = canvas.canvasy(0)
            visible_bottom = visible_top + canvas.winfo_height()
            if top + 48 > visible_bottom:
                canvas.yview_moveto(max(0, (top + 48 - canvas.winfo_height()) / region[3]))
        self.detail_title.focus_set()

    def hide_details(self):
        if not self.flush_notes():
            return
        self.detail_open = False
        self.detail.grid_remove()
        self.stage.focus_set()

    def render_details(self):
        row = self.current()
        if row is None:
            return
        row_id = row.get("id")
        self.detail_title.configure(text=f"{row.get('case', '')} / #{row.get('index', '')} · {row.get('label', '')}")
        expected = self.image_for(row, "expectedView", "expected")
        actual = self.image_for(row, "actualView", "actual")
        diff = self.image_for(row, "rawDiff" if self.raw_diff else "diff")
        message = row.get("message") or "此检查点没有可比较的本次截图"
        self.views[0].set_image(expected, "缺少标准截图或读取失败")
        self.views[1].set_image(actual, message)
        self.views[2].set_image(diff, "缺少差异图；请查看执行与比较提示")
        exact = (row.get('comparisonMetadata') or {}).get('policyVersion') == 'journal-exact'
        if exact:
            self.diff_mode.pack_forget()
            self.review_editor.grid_remove()
        else:
            self.diff_mode.pack(side='left', before=self.zoom_slider)
            self.review_editor.grid()
        exclusions = (row.get('comparisonMetadata') or {}).get('excludedRegions', [])
        self.legend.configure(text=('红色标记参与校验的变化像素；排除：' + '；'.join(
                                        f"{item['reason']} {item['rectangle']}" for item in exclusions) if exclusions else
                                    '红色标记所有变化像素；任一像素不同即校验失败') if exact else
                              '红：需重点核对的差异   橙：疑似渲染差异或弱提示，仍需人工确认')
        self.views[2].title.configure(text="整图逐像素差异（标准图底图）" if exact else
                                     "归一化原始像素差异（标准图底图）" if self.raw_diff else "降噪差异提示（标准图底图）")
        self._set_image_view()
        execution = row.get("executionStatus", "unknown")
        self.detail_execution.configure(text="执行结果：" + EXECUTION.get(execution, execution))
        errors = list(dict.fromkeys(value for (identity, _), value in self._image_errors.items() if identity == row_id))
        self.hint.configure(text=f"比较提示：{row.get('comparisonStatus', 'unknown')}\n{message}" +
                            ("\n图像读取错误：" + "; ".join(errors) if errors else ""))
        meta = row.get("comparisonMetadata") or {}
        counts = []
        for key, label in (("rawDifferencePixelCount", "原始变化"), ("reviewDifferencePixelCount", "候选变化"),
                           ("redPixelCount", "红色"), ("amberPixelCount", "橙色")):
            if key in meta:
                counts.append(f"{label} {meta[key]}")
        if meta.get("numericChanges"):
            counts.append("数值识别候选，需核对原图：" + "; ".join(
                f"{item.get('before', '')} → {item.get('after', '')}" for item in meta["numericChanges"]))
        if meta.get("ocrMessage"):
            counts.append(str(meta["ocrMessage"]))
        self.metrics.configure(text=" ｜ ".join(counts))
        saved = self.batch.decision(row)
        if row_id != self._editor_row_id:
            self.note.configure(state="normal")
            self.note.delete("1.0", "end")
            self.note.insert("1.0", saved.get("note", ""))
            self._editor_row_id, self._note_dirty = row_id, False
        readable = expected is not None and actual is not None
        can_review = self.batch.can_review(row) and readable
        if self.batch.can_review(row) and not readable:
            self.review_label.configure(text="截图无法读取 · 不可审核", text_color=COLORS["red"])
        self.note.configure(state="normal" if can_review else "disabled")
        self.reviewer_entry.configure(state="normal" if can_review else "disabled")
        for key, button in self.review_buttons.items():
            button.configure(state="normal" if can_review else "disabled",
                             fg_color=COLORS["active"] if saved.get("decision", "pending") == key else COLORS["sheet"])
        self.saved_review.configure(text="人工结论：" + (DECISIONS.get(saved.get("decision", "pending"), "待人工审核") if can_review else "不可审核") +
                                    (f" · {saved['reviewer']}" if saved.get("reviewer") else ""))
        self.save_status.configure(text="备注自动保存；审核不改写执行结果，不替换标准图", text_color=COLORS["muted"])
        if exact:
            self.saved_review.configure(text='后台比较中，完成后刷新查看结果' if execution == 'pending' else
                                        '自动校验结果已写入，无需人工确认')
            self.save_status.configure(text='查看标准图、实际图和逐像素差异；此窗口不改写自动结果或标准图')

    def set_diff_mode(self, value):
        self.raw_diff = value == "原始像素差异"
        self.draw_stage()
        if self.detail_open:
            self.render_details()

    def set_zoom(self, value):
        self.zoom = min(4., max(1., float(value)))
        self.zoom_slider.set(self.zoom)
        self._set_image_view()

    def set_pan(self, x, y):
        self.pan = (min(1., max(0., x)), min(1., max(0., y)))
        self._set_image_view()

    def _set_image_view(self):
        source_view = next((view for view in self.views if view.source is not None), None)
        if source_view is not None:
            width, height = source_view.canvas.winfo_width(), source_view.canvas.winfo_height()
            image = source_view.source
            scale = min(max(1, width - 12) / image.width, max(1, height - 12) / image.height) * self.zoom
            half_x = min(.5, width / (image.width * scale) / 2)
            half_y = min(.5, height / (image.height * scale) / 2)
            self.pan = (min(1 - half_x, max(half_x, self.pan[0])),
                        min(1 - half_y, max(half_y, self.pan[1])))
        self.zoom_label.configure(text=f"{round(self.zoom * 100)}%")
        for view in self.views:
            view.set_view(self.zoom, self.pan)

    def note_modified(self, _event=None):
        if self.note.edit_modified():
            self.note.edit_modified(False)
            self.note_changed()

    def note_changed(self, _event=None):
        if not self.current() or not self.batch.can_review(self.current()):
            return
        self._note_dirty = self.note.get("1.0", "end-1c") != self.batch.decision(self.current()).get("note", "")
        if self._note_timer is not None:
            self.after_cancel(self._note_timer)
            self._note_timer = None
        if self._note_dirty:
            self._note_timer = self.after(450, self.flush_notes)

    def flush_notes(self):
        if self._note_timer is not None:
            self.after_cancel(self._note_timer)
            self._note_timer = None
        if not self._note_dirty or self.current() is None:
            return True
        return self.save_review(self.batch.decision(self.current()).get("decision", "pending"))

    def save_review(self, decision):
        row = self.current()
        if row is None or not self.batch.can_review(row):
            return False
        if not self.reviewer.get().strip():
            self.save_status.configure(text="请填写审核人，尚未保存。", text_color=COLORS["red"])
            return False
        try:
            self.batch.save(row, decision, self.note.get("1.0", "end-1c"), reviewer=self.reviewer.get().strip())
        except (OSError, ValueError) as exc:
            self.save_status.configure(text="保存失败：" + str(exc), text_color=COLORS["red"])
            return False
        self._note_dirty = False
        self._update_status()
        self.saved_review.configure(text="人工结论：" + DECISIONS.get(decision, decision) + " · " + self.reviewer.get().strip())
        for key, button in self.review_buttons.items():
            button.configure(fg_color=COLORS["active"] if key == decision else COLORS["sheet"])
        self.save_status.configure(text="已保存 · " + self.reviewer.get().strip(), text_color=COLORS["green"])
        return True

    def close(self):
        if not self.flush_notes():
            messagebox.showerror("审核记录尚未保存", "保存失败，请修正审核人或检查目录权限后再关闭。", parent=self)
            return
        self._closed = True
        for timer in (self._animation_timer, self._note_timer):
            if timer is not None:
                self.after_cancel(timer)
        self.destroy()

    def _set_appearance_mode(self, mode_string):
        super()._set_appearance_mode(mode_string)
        if hasattr(self, "stage") and not self._closed:
            self.draw_stage()
            for view in self.views:
                view.draw()


def main(argv=None):
    parser = argparse.ArgumentParser(description="GPlatform Journal 结果查看窗口（原生 Python）")
    parser.add_argument("directory", nargs="?", type=Path, help="批次或单次回放结果目录")
    args = parser.parse_args(argv)
    ctk.set_appearance_mode("system")
    try:
        window = ReviewWindow(args.directory)
        window.mainloop()
    except tk.TclError as exc:
        print("无法启动 Tk 图形窗口：" + str(exc) + "\n请检查 Python 的 Tk 支持与当前桌面显示会话。", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
