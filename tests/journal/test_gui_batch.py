"""Batch GUI dispatch uses the maintained runner and opens its own result manifest."""
import json
from pathlib import Path
from types import SimpleNamespace
import sys
import tempfile
import threading
import unittest
from unittest.mock import Mock, patch
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools/journal'))
try:
    from journalsystem_gui import JournalSystemGUI
except ImportError:
    JournalSystemGUI = None


@unittest.skipIf(JournalSystemGUI is None, 'Requires CustomTkinter and Pillow')
class BatchGuiTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.program = self.root/'GPlatform';self.program.write_bytes(b'not executed')
        self.gui = object.__new__(JournalSystemGUI)
        g = self.gui
        g.busy=False;g.runner=None;g.last_batch_output=None;g.root=Mock();g.app_root=self.root/'App'
        g.cases=[dict(path=self.root/'assets/scenarios/a',metadata={}),dict(path=self.root/'assets/scenarios/b',metadata={})]
        g.selected_case=g.cases[0]
        g.repository=SimpleNamespace(scenarios=self.root/'assets/scenarios')
        g.application_var=SimpleNamespace(get=lambda:str(self.program))
        g.exports_var=SimpleNamespace(get=lambda:'')
        g._playbacks=lambda:self.root/'playbacks'
        g._start_task=Mock();g._session=Mock(return_value='shared-session')
        g.events=Mock();g.batch_result_label=Mock();g.cancel_event=threading.Event();g.progress_var=Mock();g._update_controls=Mock()

    def test_batch_dispatches_all_through_existing_entry_not_selected_or_filtered_case(self):
        entry=Mock()
        def main(args, *, session, progress):
            self.assertEqual(session,'shared-session')
            self.assertEqual(args[args.index('--cases')+1],'all')
            output=Path(args[args.index('--output')+1]);output.mkdir(parents=True)
            (output/'summary.json').write_text(json.dumps(dict(status='not_verified',completedCaseCount=2,plannedCaseCount=2)))
            progress(dict(index=1,total=2,case='a',phase='begin'))
        entry.main.side_effect=main
        self.gui._app_entry=Mock(return_value=entry)
        self.gui.run_batch()
        self.assertEqual(self.gui._start_task.call_args.args[0],'batch')
        result=self.gui._start_task.call_args.args[2]()
        self.assertEqual(result['plannedCaseCount'],2)
        entry.main.assert_called_once()
        self.gui.events.put.assert_called_once()

    def test_busy_and_cancel_are_shared_with_single_run(self):
        self.gui.busy=True;self.gui.run_batch();self.gui._start_task.assert_not_called()
        self.gui.task='batch';self.gui.cancel();self.assertTrue(self.gui.cancel_event.is_set())

    def test_latest_is_matched_to_program_assets_and_all_selection(self):
        base=self.gui._playbacks()
        for name,program,scope in [('session_old',self.program,'all'),('session_wrong',self.root/'other','all'),('session_single',self.program,'explicit')]:
            p=base/name;p.mkdir(parents=True)
            (p/'summary.json').write_text(json.dumps(dict(selectionScope=scope,applicationPath=str(program),
                assetsRoot=str(self.gui.repository.scenarios.parent),status='failed',completedCaseCount=1,plannedCaseCount=2)))
            (p/'screenshot_review.json').write_text('{}')
        self.gui._show_latest_batch()
        self.assertEqual(self.gui.last_batch_output,base/'session_old')
        self.assertIn('已完成 1/2',self.gui.batch_result_label.configure.call_args.kwargs['text'])

    def test_open_results_launches_native_python_viewer_for_exact_batch(self):
        self.gui.last_batch_output=self.root/'run'
        viewer=self.gui.app_root/'tools/review_results.py';viewer.parent.mkdir(parents=True);viewer.write_text('# viewer')
        with patch('journalsystem_gui.subprocess.Popen') as launch:
            self.gui.open_batch_results()
        self.assertEqual(launch.call_args.args[0],[sys.executable,str(viewer),str(self.root/'run')])


@unittest.skipUnless(JournalSystemGUI is not None and __import__('os').environ.get('GPLATFORM_REVIEW_GUI_TESTS') == '1',
                     'Native GUI checks are opt-in')
class NativeBatchButtonTests(unittest.TestCase):
    def test_real_batch_button_and_recent_result_state(self):
        import customtkinter as ctk
        import time
        with tempfile.TemporaryDirectory() as temporary:
            directory=Path(temporary);program=directory/'GPlatform';program.write_bytes(b'not executed')
            root=ctk.CTk();gui=JournalSystemGUI(root,application=program)
            self.assertEqual(gui.app_root, Path(__file__).resolve().parents[2])
            root.after_cancel(gui._load_timer)
            gui._playbacks=lambda:directory/'playbacks'
            gui.cases=[dict(path=directory/'assets/scenarios/one',metadata={})]
            gui.repository.scenarios=directory/'assets/scenarios'
            entry=Mock()
            def main(args, *, session, progress):
                output=Path(args[args.index('--output')+1]);output.mkdir(parents=True)
                summary=dict(selectionScope='all',applicationPath=str(program),assetsRoot=str(directory/'assets'),
                    status='not_verified',completedCaseCount=1,plannedCaseCount=1,screenshotReviewPendingCount=1)
                (output/'summary.json').write_text(json.dumps(summary))
                (output/'screenshot_review.json').write_text('{}')
                progress(dict(index=1,total=1,case='one',phase='begin'))
                return 2
            entry.main.side_effect=main;gui._app_entry=lambda:entry
            try:
                gui._update_controls()
                self.assertEqual(gui.batch_button.cget('state'),'normal')
                self.assertEqual(gui.batch_review_button.cget('state'),'disabled')
                gui.batch_button.invoke()
                self.assertTrue(gui.busy)
                deadline=time.monotonic()+3
                while gui.busy and time.monotonic()<deadline:root.update();time.sleep(.01)
                self.assertFalse(gui.busy)
                self.assertEqual(gui.batch_review_button.cget('state'),'normal')
                self.assertIsNotNone(gui.last_batch_output)
                self.assertEqual(entry.main.call_args.args[0][entry.main.call_args.args[0].index('--cases')+1],'all')
            finally:
                gui.busy=False;gui.close()


if __name__ == '__main__':
    unittest.main()
