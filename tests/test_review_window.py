"""Opt-in native Tk event checks, independent of the GPlatform/Journal processes.

GPLATFORM_REVIEW_GUI_TESTS=1 python -m unittest discover -s tests -p test_review_window.py
Requires a desktop session, CustomTkinter, Pillow and a Python with working Tk.
"""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import sys
import tempfile
import time
from types import SimpleNamespace
import unittest
from unittest.mock import patch

TOOLS = Path(__file__).resolve().parents[1] / "tools"
sys.path.insert(0, str(TOOLS))


@unittest.skipUnless(os.environ.get("GPLATFORM_REVIEW_GUI_TESTS") == "1", "native desktop GUI checks are opt-in")
class ReviewWindowEvents(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        from review_results import ReviewWindow
        from PIL import Image, ImageDraw
        cls.Window, cls.Image, cls.Draw = ReviewWindow, Image, ImageDraw

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.directory = Path(self.temp.name)
        for name, color in (("expected", "#dce6ef"), ("actual", "#dce6ef"),
                            ("diff", "#ee3333"), ("raw", "#ff9900")):
            image = self.Image.new("RGB", (500, 350), color)
            self.Draw.Draw(image).rectangle((180, 110, 210, 160), fill="#334455")
            image.save(self.directory / f"{name}.png")
        digest = lambda key: hashlib.sha256((self.directory / f"{key}.png").read_bytes()).hexdigest()
        self.rows = [dict(id=f"point-{i}", case="failed-case", index=i, label=f"Checkpoint {i}",
                          kind="verify.screenshot", executionStatus="captured", comparisonStatus="different",
                          reviewStatus="pending", expected="expected.png", actual="actual.png", diff="diff.png",
                          rawDiff="raw.png", baselineSha256=digest("expected"), actualSha256=digest("actual"))
                     for i in range(4)]
        self.rows[-1].update(actual=None, diff=None, rawDiff=None, executionStatus="not_run",
                             comparisonStatus="not_run", reviewStatus="not_applicable", message="Input failed")
        self.write_manifest(self.rows)
        self.window = self.Window(self.directory)
        self.window.geometry("1040x820")
        self.errors = []
        self.window.report_callback_exception = lambda kind, value, trace: self.errors.append(f"{kind.__name__}: {value}")
        self.pump(.12)

    def tearDown(self):
        self.window._note_dirty = False
        self.window.close()
        self.temp.cleanup()
        self.assertEqual(self.errors, [])

    def write_manifest(self, rows):
        (self.directory / "screenshot_review.json").write_text(json.dumps(dict(
            schemaVersion=1, scope="batch-screenshot-review", runId="native-events", status="failed",
            cases=[dict(id="failed-case", status="failed", error="Independent DB failure")], checkpoints=rows)))

    def pump(self, seconds=.04):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            self.window.update()
            time.sleep(.008)

    def wheel(self, widget, delta):
        widget.event_generate("<MouseWheel>", delta=delta, x=20, y=20)
        self.pump(.03)

    def test_native_wheel_continues_and_reverse_is_immediate(self):
        # Control only the viewer's event clock. Real Tk events/rendering may
        # exceed 240 ms on a busy build machine; wall time is not the assertion.
        clock = [time.monotonic()]
        with patch('review_results.time', SimpleNamespace(monotonic=lambda: clock[0])):
            self.wheel(self.window.stage, -120)
            self.assertEqual(self.window.selected, 1)
            clock[0] += .10
            self.wheel(self.window.stage, -120)
            self.assertEqual(self.window.selected, 1)
            self.wheel(self.window.stage, 120)
            self.assertEqual(self.window.selected, 0)
            self.wheel(self.window.stage, -120)
            self.assertEqual(self.window.selected, 1)
            clock[0] += .25
            self.wheel(self.window.stage, -120)
            self.assertEqual(self.window.selected, 2)
            clock[0] += .25
            self.wheel(self.window.stage, -120)
            self.assertEqual(self.window.selected, 3)
            clock[0] += .25
            self.wheel(self.window.stage, -120)
            self.assertEqual(self.window.selected, 3, "end must not wrap to the first checkpoint")

    def test_hover_whole_pair_and_click_inline_details(self):
        stage = self.window.stage
        x, y = stage.winfo_width() // 2, stage.winfo_height() // 2
        for position in (x, x - 220, x + 220, x):
            stage.event_generate("<Motion>", x=position, y=y)
            self.pump()
            self.assertTrue(self.window.hovered, "both images and their gap belong to one stable hover area")
        stage.event_generate("<Button-1>", x=x, y=y)
        self.pump()
        self.assertTrue(self.window.detail_open)
        self.assertTrue(self.window.stage.winfo_ismapped(), "detail must not replace the reel")
        selected = self.window.selected
        before_scroll = self.window.body._parent_canvas.yview()
        self.wheel(self.window.views[0].canvas, -120)
        self.assertEqual(self.window.selected, selected, "detail wheel must not select a different checkpoint")
        self.assertNotEqual(self.window.body._parent_canvas.yview(), before_scroll,
                            "fit-scale image wheel should scroll the detail document normally")
        self.window.event_generate("<Escape>")
        self.pump()
        self.assertFalse(self.window.detail_open)
        self.assertEqual(self.window.selected, selected)

    def test_notes_reviews_persist_without_changing_execution(self):
        win = self.window
        win.show_details()
        win.reviewer.set("GUI reviewer")
        win.note.insert("1.0", "First checkpoint note")
        win.note_changed()
        win.select(1)  # Navigation must commit the dirty note against the previous checkpoint.
        self.pump()
        saved = json.loads((self.directory / "review-decisions.json").read_text())
        self.assertEqual(saved["reviews"]["point-0"]["note"], "First checkpoint note")
        self.assertEqual(win.note.get("1.0", "end-1c"), "")
        win.review_buttons["accepted"].invoke()
        self.assertEqual(win.batch.decision(win.current())["decision"], "accepted")
        self.assertEqual(win.batch.document["status"], "failed")
        self.assertIn("failed", win.title())
        win.select(0)
        self.assertEqual(win.note.get("1.0", "end-1c"), "First checkpoint note")
        win.select(3)
        self.assertEqual(win.review_buttons["accepted"].cget("state"), "disabled")
        self.assertIsNotNone(win.views[0].source)
        self.assertIsNone(win.views[1].source)
        self.assertIsNone(win.views[2].source)

    def test_diff_switch_and_synced_zoom_pan(self):
        win = self.window
        win.show_details()
        self.pump()
        win.set_diff_mode("原始像素差异")
        self.assertEqual(win.views[2].source.getpixel((0, 0)), (255, 153, 0))
        win.set_zoom(2)
        canvas = win.views[1].canvas
        canvas.event_generate("<ButtonPress-1>", x=110, y=100)
        canvas.event_generate("<B1-Motion>", x=70, y=80)
        self.pump()
        self.assertNotEqual(win.pan, (.5, .5))
        self.assertTrue(all(view.center == win.pan and view.zoom == 2 for view in win.views))
        win.set_zoom(1)
        self.assertEqual(win.zoom, 1)

    def test_database_only_batch_has_visible_automatic_results(self):
        self.write_manifest([])
        self.window.reload_batch()
        self.pump()
        self.assertFalse(self.window.browser.winfo_ismapped())
        self.assertTrue(self.window.auto_results.winfo_ismapped())
        text = self.window.auto_text.get("1.0", "end-1c")
        self.assertIn("failed-case", text)
        self.assertIn("Independent DB failure", text)
        self.assertIn("failed", self.window.title())

    def test_exact_results_show_pixel_failure_without_requesting_manual_review(self):
        rows = [dict(self.rows[0], reviewStatus='not_applicable', automaticStatus='failed', executionStatus='failed',
                     comparisonMetadata=dict(policyVersion='journal-exact', rawDifferencePixelCount=17))]
        self.write_manifest(rows)
        self.window.reload_batch()
        self.window.show_details()
        self.pump()
        self.assertEqual(self.window.execution_label.cget('text'), '截图校验失败')
        self.assertIn('差异像素 17', self.window.review_label.cget('text'))
        self.assertIn('失败 1', self.window.summary.get())
        self.assertNotIn('不可用', self.window.review_label.cget('text'))
        self.assertIn('整图逐像素', self.window.views[2].title.cget('text'))
        self.assertEqual(self.window.review_buttons['accepted'].cget('state'), 'disabled')

@unittest.skipUnless(all(importlib.util.find_spec(name) for name in ('_tkinter', 'customtkinter', 'PIL')),
                     'requires optional viewer dependencies; no desktop session is needed')
class JournalRecentResultTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.directory = Path(temporary.name)

    def test_journal_recent_result_opens_explicit_batch_and_single_replay(self):
        from unittest.mock import Mock
        sys.path.insert(0, str(TOOLS / 'journal'))
        from journalsystem_gui import JournalSystemGUI
        from session_repository import JournalSessionRepository
        playbacks = self.directory / 'playbacks'
        custom = self.directory / 'custom'
        custom.mkdir()
        app, assets = self.directory / 'app', self.directory / 'assets'
        (custom / 'summary.json').write_text(json.dumps(dict(selectionScope='explicit',
            applicationPath=str(app), assetsRoot=str(assets), status='failed', completedCaseCount=1, plannedCaseCount=1)))
        (custom / 'screenshot_review.json').write_text('{}')
        repository = JournalSessionRepository(playbacks=playbacks)
        repository.remember_result(custom)
        gui = SimpleNamespace(application_var=SimpleNamespace(get=lambda: str(app)),
            repository=SimpleNamespace(scenarios=assets / 'scenarios', recordings=self.directory / 'recordings'),
            _playbacks=lambda: playbacks, batch_result_label=Mock())
        JournalSystemGUI._show_latest_batch(gui)
        self.assertEqual(gui.last_batch_output, custom.resolve())
        raw = self.directory / 'raw'
        raw.mkdir()
        execution = dict(acceptance='failed', completedEntries=82, totalEntries=82,
                         applicationPath=str(app), journalPath=str(assets / 'scenarios/model/events/Journal.json'))
        (raw / 'execution_result.json').write_text(json.dumps(execution))
        repository.remember_result(raw)
        JournalSystemGUI._show_latest_batch(gui)
        self.assertEqual(gui.last_batch_output, raw.resolve())
        self.assertIn('82/82', gui.batch_result_label.configure.call_args.kwargs['text'])
        for changes in [dict(applicationPath=str(self.directory / 'other-app')),
                        dict(applicationPath=''),
                        dict(journalPath=str(self.directory / 'other-assets/scenarios/model/events/Journal.json')),
                        dict(journalPath='')]:
            with self.subTest(changes=changes):
                (raw / 'execution_result.json').write_text(json.dumps(dict(execution, **changes)))
                JournalSystemGUI._show_latest_batch(gui)
                self.assertIsNone(gui.last_batch_output)
        (raw / 'execution_result.json').write_text(json.dumps(dict(execution,
            journalPath=str(gui.repository.recordings / 'session_recorded/Journal.json'))))
        JournalSystemGUI._show_latest_batch(gui)
        self.assertEqual(gui.last_batch_output, raw.resolve())


if __name__ == "__main__":
    unittest.main()
