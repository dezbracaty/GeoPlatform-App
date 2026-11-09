import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools/journal"))

from journal_display_profile import (WINDOW_PROFILE, process_environment,
                                     validate_preconditions, wait_for_window)
from journal_execution_session import JournalExecutionSession
from journal_runner import JournalRunner, write_journal
from journalsystem import JournalSystem


def window(dpr=1, size=None):
    return {"objectName": "gplatform.main", "logicalSize": size or [1382, 954],
            "devicePixelRatio": dpr, "availableLogicalSize": [1920, 1080]}


class DisplayProfileCheck(unittest.TestCase):
    def test_explicit_child_environment_is_applied_after_cleanup_without_mutating_inputs(self):
        inherited = {"QT_SCALE_FACTOR": "4", "QT_FONT_DPI": "192", "PATH": "/fixture"}
        scenario = {"QT_SCALE_FACTOR": "3", "GP_LOG_CONSOLE": "1"}
        explicit = {"QT_SCALE_FACTOR": "1", "QT_FONT_DPI": "96", "QT_ENABLE_HIGHDPI_SCALING": "0"}
        result = process_environment(inherited, scenario, environment_overrides=explicit)
        self.assertEqual({key: result[key] for key in explicit}, explicit)
        self.assertEqual(result["GP_LOG_CONSOLE"], "1")
        self.assertEqual(inherited["QT_SCALE_FACTOR"], "4")
        self.assertEqual(scenario["QT_SCALE_FACTOR"], "3")
        self.assertNotIn("QT_SCALE_FACTOR", process_environment(inherited, scenario))
        for invalid in (["not", "a", "mapping"], {"A=B": "x"}, {"": "x"}, {"A": 1}, {"A": "x\0y"}):
            with self.subTest(invalid=invalid), self.assertRaises(ValueError):
                process_environment({}, {}, environment_overrides=invalid)

    def test_explicit_environment_requires_an_owned_application(self):
        with tempfile.TemporaryDirectory() as temporary:
            journal = write_journal(Path(temporary) / "Journal.json", [
                JournalSystem.entry("action", {"code": "existing.action"})])
            with self.assertRaisesRegex(ValueError, "requires launching"):
                JournalExecutionSession().run(journal, port=9999, environment_overrides={"EXAMPLE": "1"})

    def test_native_environment_preserves_business_setup_and_accepts_legacy_baselines(self):
        inherited = {"QT_SCALE_FACTOR": "4", "QT_SCREEN_SCALE_FACTORS": "display=3",
                     "QT_DEVICE_PIXEL_RATIO": "2", "PATH": "/usr/bin"}
        result = process_environment(inherited, {"GP_LOG_CONSOLE": "1", "QT_SCALE_FACTOR": "0.5"})
        self.assertNotIn("QT_SCALE_FACTOR", result)
        self.assertNotIn("QT_SCREEN_SCALE_FACTORS", result)
        self.assertEqual(result["GP_LOG_CONSOLE"], "1")
        self.assertNotIn("QT_DEVICE_PIXEL_RATIO", result)
        self.assertEqual(inherited["QT_SCALE_FACTOR"], "4")
        validate_preconditions({"mainWindowLogicalSize": [1382, 954], "devicePixelRatio": 1})
        validate_preconditions({"devicePixelRatio": 2, "imageSize": [2764, 1908]})
        for invalid in ({"devicePixelRatio": 0}, {"imageSize": [2764, 0]},
                        {"mainWindowLogicalSize": [1395, 887]}):
            with self.subTest(invalid=invalid), self.assertRaises((ValueError, RuntimeError)):
                validate_preconditions(invalid)

    def test_window_settles_at_native_dpr_but_invalid_dpr_or_size_never_admits_input(self):
        client = Mock()
        client.window_info.side_effect = [window(size=[1300, 900]), window()]
        self.assertEqual(wait_for_window(client), window())
        client.window_info = Mock(return_value=window(2))
        self.assertEqual(wait_for_window(client), window(2))
        for invalid in (window(float("nan")), window(True)):
            client.window_info = Mock(return_value=invalid)
            with self.subTest(invalid=invalid), self.assertRaises(RuntimeError):
                wait_for_window(client)
        client.window_info = Mock(return_value=window(size=[1300, 900]))
        with self.assertRaisesRegex(RuntimeError, "did not reach"):
            wait_for_window(client, timeout=0)
        client.execute_entry.assert_not_called()

    def test_screen_too_small_is_rejected_even_when_qt_reports_target_geometry(self):
        client = Mock()
        client.window_info.return_value = dict(window(), availableLogicalSize=[1280, 660])
        with self.assertRaisesRegex(RuntimeError, "available screen area is 1280x660"):
            wait_for_window(client)
        client.execute_entry.assert_not_called()

    def test_owned_instance_preserves_native_dpr_without_scaling_or_restart(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            journal = write_journal(root / "Journal.json", [
                JournalSystem.entry("action", {"code": "existing.action"})])
            client = Mock()
            client.window_info.return_value = window(2)
            client.paths.return_value = {"recordings": str(root / "recordings"),
                                         "playbacks": str(root / "playbacks")}
            client.request.side_effect = lambda method, params, **kwargs: {
                "replayId": params["replayId"], "status": "ready"}
            processes = [Mock(pid=101, returncode=None)]
            for process in processes:
                process.poll.return_value = None
                process.wait.side_effect = lambda process=process, **kwargs: setattr(
                    process.poll, "return_value", -15)
            with patch("journal_execution_session.JournalSystem", return_value=client), \
                    patch("journal_execution_session._activate_owned_macos_application", return_value=None), \
                    patch("journal_execution_session.subprocess.Popen", side_effect=processes) as launch:
                result = JournalExecutionSession().run(journal, application=sys.executable,
                                                       output_dir=root / "result")
            self.assertTrue(result["success"], result.get("error"))
            launch.assert_called_once()
            self.assertNotIn("QT_SCALE_FACTOR", launch.call_args.kwargs["env"])
            self.assertEqual(result["actualWindow"], window(2))
            client.execute_entry.assert_called_once()
            self.assertEqual(client.request.call_args.args[1]["windowProfile"], WINDOW_PROFILE)
            for process in processes:
                process.terminate.assert_called_once()

    def test_existing_native_dpr_is_admitted_without_launch_or_scaling(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            journal = write_journal(root / "Journal.json", [
                JournalSystem.entry("action", {"code": "existing.action"})])
            client = Mock()
            client.window_info.return_value = window(2)
            client.paths.return_value = {"playbacks": str(root / "playbacks")}
            with patch("journal_execution_session.JournalSystem", return_value=client), \
                    patch("journal_execution_session.subprocess.Popen") as launch:
                result = JournalExecutionSession().run(journal, port=9999, output_dir=root / "result")
            self.assertTrue(result["success"], result.get("error"))
            launch.assert_not_called()
            client.begin_replay.assert_called_once_with(timeout=30, window_profile=WINDOW_PROFILE)
            client.execute_entry.assert_called_once()

    def test_recording_checks_profile_and_stops_on_failed_window_preparation(self):
        with tempfile.TemporaryDirectory() as temporary:
            client = Mock()
            client.paths.return_value = {"recordings": temporary}
            client.window_info.return_value = window(float("nan"))
            runner = JournalRunner(client)
            with self.assertRaisesRegex(RuntimeError, "Invalid application window DPR"):
                runner.start_recording()
            client.start_recording.assert_called_once_with(True, window_profile=WINDOW_PROFILE)
            client.stop_recording.assert_called_once()
            self.assertFalse(runner.recording)


if __name__ == "__main__":
    unittest.main()
