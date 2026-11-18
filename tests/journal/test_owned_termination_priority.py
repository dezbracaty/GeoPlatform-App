"""Owned cleanup normalizes inherited Windows priority without changing replay."""
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools/journal'))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import journal_execution_session as sessions
from test_python_client import RunnerClient, write_journal


class OwnedTerminationCheck(unittest.TestCase):
    def process(self):
        process = Mock(pid=123, returncode=None)
        process.poll.return_value = None
        def stopped(**kwargs):
            process.returncode = 1
            process.poll.return_value = 1
            return 1
        process.wait.side_effect = stopped
        return process

    def test_only_low_priority_owned_termination_is_temporarily_normalized(self):
        for original in (0x40, 0x4000, 0x20, 0x8000, 0x80, 0x100):
            with self.subTest(original=original):
                process, result, calls = self.process(), {}, []
                def priority(child, value=None):
                    self.assertIs(child, process)
                    calls.append(('priority', value))
                    return original if value is None else value
                process.terminate.side_effect = lambda: calls.append(('terminate', None))
                with patch.object(sessions.sys, 'platform', 'win32'), \
                        patch.object(sessions, '_windows_priority_class', side_effect=priority):
                    sessions._terminate_owned_application(process, result)
                expected = [('priority', None)]
                if original in (0x40, 0x4000): expected.append(('priority', 0x20))
                self.assertEqual(calls, expected + [('terminate', None)])
                process.wait.assert_called_once_with(timeout=5)

    def test_termination_failure_restores_original_class_and_preserves_failure(self):
        for fail_at in ('terminate', 'wait'):
            with self.subTest(fail_at=fail_at):
                process, result = Mock(), {}
                process.poll.return_value = None
                error = OSError('cannot terminate') if fail_at == 'terminate' else subprocess.TimeoutExpired('owned', 5)
                getattr(process, fail_at).side_effect = error
                with patch.object(sessions.sys, 'platform', 'win32'), \
                        patch.object(sessions, '_windows_priority_class', return_value=0x4000) as priority:
                    with self.assertRaises(type(error)):
                        sessions._terminate_owned_application(process, result)
                self.assertEqual([c.args for c in priority.call_args_list],
                    [(process,), (process, 0x20), (process, 0x4000)])
                self.assertEqual(result['applicationTerminationPriority']['restoredClass'], 0x4000)

    def test_priority_api_failure_keeps_existing_termination_and_wait(self):
        for replies in ([OSError('cannot query')], [0x4000, OSError('cannot set')]):
            with self.subTest(replies=replies):
                process, result = self.process(), {}
                with patch.object(sessions.sys, 'platform', 'win32'), \
                        patch.object(sessions, '_windows_priority_class', side_effect=replies):
                    sessions._terminate_owned_application(process, result)
                process.terminate.assert_called_once()
                process.wait.assert_called_once_with(timeout=5)
                self.assertIn('error', result['applicationTerminationPriority'])

    def test_restore_error_is_retained_without_hiding_timeout(self):
        process, result = Mock(), {}
        process.poll.return_value = None
        process.wait.side_effect = subprocess.TimeoutExpired('owned', 5)
        with patch.object(sessions.sys, 'platform', 'win32'), \
                patch.object(sessions, '_windows_priority_class', side_effect=[0x4000, 0x20, OSError('restore failed')]):
            with self.assertRaises(subprocess.TimeoutExpired):
                sessions._terminate_owned_application(process, result)
        self.assertIn('restore failed', ' '.join(result['cleanupErrors']))
        self.assertNotIn('restoredClass', result['applicationTerminationPriority'])

    def test_session_final_exit_code_timeout_and_instance_boundaries(self):
        for mode in ('owned', 'keep', 'attach', 'mac', 'timeout', 'timeout-exited'):
            with self.subTest(mode=mode), tempfile.TemporaryDirectory() as temporary:
                root, process = Path(temporary), self.process()
                client = RunnerClient(root)
                client.connect, client.disconnect = Mock(), Mock()
                client.request = Mock(side_effect=lambda method, params, **kwargs:
                    {'status': 'ready', 'replayId': params['replayId']})
                if mode == 'timeout': process.wait.side_effect = subprocess.TimeoutExpired('owned', 5)
                if mode == 'timeout-exited':
                    def exited_at_deadline(**kwargs):
                        process.returncode = 1
                        process.poll.return_value = 1
                        raise subprocess.TimeoutExpired('owned', 5)
                    process.wait.side_effect = exited_at_deadline
                journal = write_journal(root/'source.json', [client.entry('action', {'code': 'existing.action'})])
                with patch.object(sessions.sys, 'platform', 'darwin' if mode == 'mac' else 'win32'), \
                        patch.object(sessions, 'JournalSystem', return_value=client), \
                        patch.object(sessions.subprocess, 'Popen', return_value=process), \
                        patch.object(sessions, '_windows_priority_class', return_value=0x4000) as priority:
                    result = sessions.JournalExecutionSession().run(journal,
                        application=None if mode == 'attach' else sys.executable,
                        port=9999 if mode == 'attach' else 0, keep_app=mode == 'keep', output_dir=root/'output')
                if mode in ('keep', 'attach', 'mac'): priority.assert_not_called()
                if mode in ('keep', 'attach'): process.terminate.assert_not_called()
                elif mode == 'timeout':
                    self.assertFalse(result['success'])
                    self.assertTrue(result['applicationKeptOpen'])
                    self.assertIsNone(result['applicationExitCode'])
                    self.assertIn('timed out after 5', ' '.join(result['cleanupErrors']))
                else:
                    self.assertTrue(result['success'])
                    self.assertEqual(result['applicationExitCode'], 1)
                    self.assertFalse(result['applicationKeptOpen'])
                if mode.startswith('timeout'):
                    self.assertIn('timed out after 5', result['applicationTerminationTimeout'])
                if mode == 'timeout-exited':
                    self.assertNotIn('cleanupErrors', result)
                self.assertEqual([c.args[0] for c in client.request.call_args_list],
                                 [] if mode == 'attach' else ['journal.replay.begin'])
                if result.get('applicationLiveLogPath'): Path(result['applicationLiveLogPath']).unlink()


if __name__ == '__main__':
    unittest.main()
