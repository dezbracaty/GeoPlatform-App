"""Run the delivery App natively through the dedicated regression entry."""
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts/ci'))
from mac_regression import regression


class MacRegressionTests(unittest.TestCase):
    def test_regression_uses_native_delivery_app_and_all_pinned_assets(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);source=root/'source';source.mkdir()
            bundle=root/'GPlatform.app';exe=bundle/'Contents/MacOS/GPlatform';exe.parent.mkdir(parents=True);exe.write_bytes(b'program')
            tools=source/'tools/journal';tools.mkdir(parents=True)
            for name in ['journal_execution_session.py','journal_runner.py','journal_display_profile.py']:(tools/name).write_text('# managed')
            output=root/'evidence'
            env=dict(GPLATFORM_JOURNAL_TOOLS=str(root/'obsolete-tools'),QT_QPA_PLATFORM='offscreen',QT_SCALE_FACTOR='.5')
            def execute(selected, program, destination, environment, *, assets):
                self.assertEqual(selected, source)
                self.assertEqual(program, exe)
                self.assertEqual(assets, root / "host-source/App/tests/TestAssets")
                self.assertNotIn('QT_QPA_PLATFORM', environment)
                self.assertNotIn('QT_SCALE_FACTOR', environment)
                return dict(completedCaseCount=101), output / 'run/session_fixture'
            with patch('mac_regression.sys.platform','darwin'),patch('mac_regression.getpass.getuser',return_value='ci'),\
                    patch('mac_regression.subprocess.check_output',return_value='ci\n'),patch('mac_regression.run'),\
                    patch('mac_regression.text_git',return_value='b'*40),\
                    patch('mac_regression.server_regression',side_effect=execute):
                result=regression(source,bundle,output,env, assets=root / "host-source/App/tests/TestAssets", assets_sha='a'*40)
            self.assertEqual(result['case_count'],101)
            self.assertEqual(exe.read_bytes(),b'program')

    def test_incomplete_or_failed_regression_is_not_accepted(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);source=root/'source';source.mkdir();tools=source/'tools/journal';tools.mkdir(parents=True)
            (tools/'journal_execution_session.py').write_text('# managed')
            bundle=root/'App.app';exe=bundle/'Contents/MacOS/GPlatform';exe.parent.mkdir(parents=True);exe.write_bytes(b'app')
            for name in ['journal_runner.py','journal_display_profile.py']:(tools/name).write_text('# managed')
            with patch('mac_regression.sys.platform','darwin'),patch('mac_regression.getpass.getuser',return_value='ci'),\
                    patch('mac_regression.subprocess.check_output',return_value='ci\n'),patch('mac_regression.run'),\
                    patch('mac_regression.text_git',return_value='b'*40),\
                    patch('mac_regression.server_regression',side_effect=RuntimeError('Server regression did not pass')):
                with self.assertRaisesRegex(RuntimeError,'did not pass'):
                    regression(source,bundle,root/'out',dict(GPLATFORM_JOURNAL_TOOLS=str(tools)),
                               assets=root / "host-source/App/tests/TestAssets", assets_sha='a'*40)
