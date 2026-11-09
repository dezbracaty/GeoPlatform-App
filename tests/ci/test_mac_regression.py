"""Parallel packaging/regression preserves both results and runs the delivery App natively."""
import json
from pathlib import Path
import sys
import tempfile
import threading
import unittest
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts/ci'))
from mac_regression import package_and_regress, prepare_assets, regression


class MacRegressionTests(unittest.TestCase):
    def test_assets_use_independent_pinned_commit_without_changing_app_gitlink(self):
        source = Path('/fixture/source')
        declared, selected = 'a' * 40, 'b' * 40
        with patch('mac_regression.text_git', side_effect=[declared, declared, selected]), \
                patch('mac_regression.git_env', return_value={}), patch('mac_regression.run') as execute:
            self.assertEqual(prepare_assets(source, selected), selected)
        commands = [call.args[0] for call in execute.call_args_list]
        self.assertIn(['git', '-C', source / 'tests/TestAssets', 'fetch', '--depth=1', 'origin', selected], commands)
        self.assertIn(['git', '-C', source / 'tests/TestAssets', 'checkout', '--detach', selected], commands)
        self.assertEqual(commands[-1], ['git', '-C', source / 'tests/TestAssets', 'lfs', 'pull'])
        self.assertFalse(any('add' in command or 'commit' in command for command in commands))

    def test_wrong_asset_checkout_is_rejected_before_lfs_or_replay(self):
        with patch('mac_regression.text_git', side_effect=['a' * 40, 'a' * 40, 'c' * 40]), \
                patch('mac_regression.git_env', return_value={}), patch('mac_regression.run') as execute:
            with self.assertRaisesRegex(RuntimeError, 'prepared regression SHA'):
                prepare_assets(Path('/fixture/source'), 'b' * 40)
        self.assertFalse(any('lfs' in call.args[0] for call in execute.call_args_list))

    def test_both_stages_start_in_parallel_and_success_is_preserved(self):
        barrier=threading.Barrier(2)
        with tempfile.TemporaryDirectory() as temporary:
            result=package_and_regress(lambda:(barrier.wait(timeout=3),dict(package='App.dmg'))[1],
                lambda:(barrier.wait(timeout=3),dict(status='passed',case_count=101))[1],Path(temporary)/'stages.json')
            self.assertEqual(result['installer']['status'],'passed')
            self.assertEqual(result['regression']['case_count'],101)
            self.assertEqual(json.loads((Path(temporary)/'stages.json').read_text()),result)

    def test_failure_does_not_cancel_other_result_or_turn_it_into_success(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary)
            def package():
                (root/'App.dmg').write_bytes(b'preserved installer')
                return dict(package='App.dmg')
            def verify():raise RuntimeError('screenshot comparison failed')
            result=package_and_regress(package,verify,root/'stages.json')
            self.assertEqual(result['installer']['status'],'passed')
            self.assertEqual(result['regression']['status'],'failed')
            self.assertIn('screenshot',result['regression']['error'])
            self.assertTrue((root/'App.dmg').is_file())

    def test_regression_uses_native_delivery_app_and_all_pinned_assets(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);source=root/'source';source.mkdir()
            bundle=root/'GPlatform.app';exe=bundle/'Contents/MacOS/GPlatform';exe.parent.mkdir(parents=True);exe.write_bytes(b'program')
            tools=source/'tools/journal';tools.mkdir(parents=True)
            for name in ['journal_execution_session.py','journal_runner.py','journal_display_profile.py']:(tools/name).write_text('# managed')
            output=root/'evidence'
            env=dict(GPLATFORM_JOURNAL_TOOLS=str(root/'obsolete-tools'),QT_QPA_PLATFORM='offscreen',QT_SCALE_FACTOR='.5')
            def execute(args,**kwargs):
                self.assertNotIn('QT_QPA_PLATFORM',kwargs['env'])
                self.assertNotIn('QT_SCALE_FACTOR',kwargs['env'])
                self.assertIn(str(exe),args)
                self.assertEqual(args[args.index('--cases')+1],'all')
                self.assertEqual(args[args.index('--journal-tools')+1],str(source/'tools/journal'))
                run=Path(args[args.index('--output')+1]);run.mkdir()
                (run/'summary.json').write_text(json.dumps(dict(status='passed',runComplete=True,completedCaseCount=101)))
                return type('Result',(),{'returncode':0})()
            with patch('mac_regression.sys.platform','darwin'),patch('mac_regression.getpass.getuser',return_value='ci'),\
                    patch('mac_regression.subprocess.check_output',return_value='ci\n'),patch('mac_regression.run'),\
                    patch('mac_regression.prepare_assets',return_value='a'*40),patch('mac_regression.text_git',return_value='b'*40),\
                    patch('mac_regression.subprocess.run',side_effect=execute):
                result=regression(source,bundle,output,env)
            self.assertEqual(result['case_count'],101)
            self.assertEqual(exe.read_bytes(),b'program')

    def test_incomplete_or_failed_regression_is_not_accepted(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);source=root/'source';source.mkdir();tools=source/'tools/journal';tools.mkdir(parents=True)
            (tools/'journal_execution_session.py').write_text('# managed')
            bundle=root/'App.app';exe=bundle/'Contents/MacOS/GPlatform';exe.parent.mkdir(parents=True);exe.write_bytes(b'app')
            for name in ['journal_runner.py','journal_display_profile.py']:(tools/name).write_text('# managed')
            def execute(args,**kwargs):
                run=Path(args[args.index('--output')+1]);run.mkdir()
                (run/'summary.json').write_text(json.dumps(dict(status='passed',runComplete=False,completedCaseCount=1)))
                return type('Result',(),{'returncode':0})()
            with patch('mac_regression.sys.platform','darwin'),patch('mac_regression.getpass.getuser',return_value='ci'),\
                    patch('mac_regression.subprocess.check_output',return_value='ci\n'),patch('mac_regression.run'),\
                    patch('mac_regression.prepare_assets',return_value='a'*40),patch('mac_regression.text_git',return_value='b'*40),\
                    patch('mac_regression.subprocess.run',side_effect=execute):
                with self.assertRaisesRegex(RuntimeError,'did not pass'):
                    regression(source,bundle,root/'out',dict(GPLATFORM_JOURNAL_TOOLS=str(tools)))
