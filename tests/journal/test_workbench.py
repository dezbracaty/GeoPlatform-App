"""Task ownership, saved evidence, and authenticated browser transport."""
import hashlib
import http.client
import io
import json
from pathlib import Path
import sys
import tempfile
import threading
import time
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools/journal'))
from journal_workbench import JournalWorkbench, WorkbenchError
from journal_server import WorkbenchServer, connection_code, main as server_main


class ConnectionCodeTests(unittest.TestCase):
    def test_generated_codes_keep_leading_zeroes_and_six_digits(self):
        with patch('journal_server.secrets.randbelow', side_effect=[0, 42, 999999]) as random:
            self.assertEqual([connection_code() for _ in range(3)], ['000000', '000042', '999999'])
            self.assertEqual(random.call_count, 3)
            random.assert_called_with(1_000_000)

    def test_configured_code_requires_exact_ascii_digits(self):
        self.assertEqual(connection_code('012345'), '012345')
        for value in ('', '12345', '1234567', '１２３４５６', '12345a', ' 012345', 123456, True):
            with self.subTest(value=value), self.assertRaises(ValueError):
                connection_code(value)

    def test_invalid_cli_code_is_rejected_before_loading_workbench(self):
        with patch('journal_server.JournalWorkbench') as create, patch('sys.stderr', new_callable=io.StringIO):
            with self.assertRaises(SystemExit) as error:
                server_main(['--token', 'old-long-access-code', '--no-browser'])
        self.assertEqual(error.exception.code, 2)
        create.assert_not_called()


class WorkbenchTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        self.program = self.root / 'GPlatform'
        self.program.write_bytes(b'not executed')
        with patch.object(JournalWorkbench, 'load_cases'):
            self.workbench = JournalWorkbench(self.program, self.root / 'App', output_root=self.root / 'runs')
        self.w = self.workbench
        self.w.repository.recordings = self.root / 'recordings'
        self.w.application_hash = hashlib.sha256(self.program.read_bytes()).hexdigest()
        self.w.cases = [dict(id=n, name=n, error=None, needsExports=False, latestStatus=None) for n in ('a', 'b')]
        self.w.case_hashes = {'a': {'journal': 'input-a'}, 'b': {'journal': 'input-b'}}

    def wait(self):
        self.w.worker.join(3)
        self.assertFalse(self.w.worker.is_alive())

    def summary(self, output, status='passed', planned=None, cases=None):
        output.mkdir(parents=True, exist_ok=True)
        data = dict(status=status, applicationPath=str(self.program), assetsRoot=str(self.w.assets),
            plannedCases=planned or ['a', 'b'], cases=cases or [], reviewRunId='test-run')
        (output / 'summary.json').write_text(json.dumps(data))
        return data

    def test_selected_batch_uses_formal_entry_and_browser_disconnection_does_not_cancel(self):
        entered, release = threading.Event(), threading.Event()
        def main(args, *, session, progress, between_cases):
            self.assertEqual(args[args.index('--cases') + 1], 'b,a')
            progress(dict(case='b', index=1, total=2, phase='begin'))
            entered.set()
            release.wait(2)
            self.assertFalse(session.cancel_event.is_set())
            output = Path(args[args.index('--output') + 1])
            self.summary(output, cases=[dict(id='b', status='passed'), dict(id='a', status='passed')])
            return 0
        with patch('journal_workbench.run_acceptance.main', side_effect=main):
            self.w.run(dict(cases=['b', 'a']))
            self.assertTrue(entered.wait(2))
            self.assertEqual(self.w.state()['task']['caseIndex'], 1)
            with self.assertRaises(WorkbenchError):
                self.w.run(dict(cases=['a']))
            with self.assertRaises(WorkbenchError):
                self.w.configure(dict(exports=''))
            release.set()
            self.wait()
        state = self.w.state()
        self.assertEqual(state['task']['state'], 'completed')
        self.assertEqual([r['id'] for r in state['task']['rows']], ['b', 'a'])

    def test_pause_refreshes_background_comparisons_and_cancel_wakes_boundary(self):
        refreshed = threading.Event()
        def main(args, *, session, progress, between_cases):
            between_cases(refreshed.set)
            self.assertTrue(session.cancel_event.is_set())
            self.summary(Path(args[args.index('--output') + 1]), status='not_verified')
            return 2
        with patch('journal_workbench.run_acceptance.main', side_effect=main):
            self.w.run(dict(cases=['a', 'b'], pauseAfterEach=True))
            self.assertTrue(refreshed.wait(2))
            self.assertEqual(self.w.state()['task']['state'], 'paused')
            self.w.cancel()
            self.wait()
        self.assertEqual(self.w.task['state'], 'cancelled')
        self.assertTrue(all(row['status'] != 'passed' for row in self.w.state()['task']['rows']))

    def test_resume_keeps_same_batch_and_boundary_only_pauses_when_requested(self):
        paused, done = threading.Event(), threading.Event()
        def main(args, *, session, progress, between_cases):
            between_cases(paused.set)
            done.set()
            self.summary(Path(args[args.index('--output') + 1]))
            return 0
        with patch('journal_workbench.run_acceptance.main', side_effect=main):
            self.w.run(dict(cases=['a', 'b'], pauseAfterEach=True))
            self.assertTrue(paused.wait(2))
            identifier = self.w.task['id']
            self.w.resume()
            self.wait()
        self.assertTrue(done.is_set())
        self.assertEqual(self.w.task['id'], identifier)

    def test_recording_and_unsaved_checkpoint_session_block_batch_and_shutdown(self):
        self.w.runner = SimpleNamespace(recording=True, has_pending_save=False,
            session_path=self.root / 'active')
        with self.assertRaises(WorkbenchError):
            self.w.run(dict(cases=['a']))
        with self.assertRaises(WorkbenchError):
            self.w.close()
        self.assertTrue(self.w.runner.recording)

    def test_pending_local_save_can_be_retried_after_connection_loss(self):
        called = threading.Event()
        def save(metadata):
            called.set()
            self.w.runner.has_pending_save = False
            return self.root / 'Journal.json'
        self.w.runner = SimpleNamespace(recording=False, has_pending_save=True,
            session_path=self.root / 'active', stop_recording=save)
        self.w.record_stop({})
        self.wait()
        self.assertTrue(called.is_set())
        self.assertEqual(self.w.task['state'], 'completed')

    def test_record_operations_reuse_runner_and_keep_candidate_state(self):
        class Runner:
            recording = False
            has_pending_save = False
            session_path = self.root / 'session_1'
            def start_recording(s):
                s.recording = True
                return s.session_path
            def record_screenshot(s, label):
                return dict(atMs=123, sequence=1)
            def stop_recording(s, metadata):
                s.recording = False
                return s.session_path / 'events/Journal.json'
        self.w.client = SimpleNamespace(client=True)
        self.w.runner = Runner()
        self.w.record_start(dict(name='name'))
        self.wait()
        self.w.record_checkpoint(dict(kind='screenshot', label='image'))
        self.wait()
        self.assertEqual(self.w.state()['recording']['checkpoints'][0]['atMs'], 123)
        self.w.record_stop({})
        self.wait()
        self.assertFalse(self.w.state()['recording']['active'])
        self.assertEqual(self.w.task['kind'], 'record-stop')

    def test_evidence_cannot_escape_registered_run_even_via_symlink(self):
        output = self.root / 'run'
        output.mkdir()
        secret = self.root / 'private.txt'
        secret.write_text('private')
        (output / 'escape').symlink_to(secret)
        identifier = self.w._register_run(output)
        for path in ('../private.txt', str(secret), 'escape', '..\\private.txt'):
            with self.assertRaises((WorkbenchError, FileNotFoundError)):
                self.w.artifact(identifier, path)
        with self.assertRaises(FileNotFoundError):
            self.w.artifact('unregistered', 'private.txt')

    def test_result_uses_saved_status_includes_not_run_and_keeps_review_separate(self):
        output = self.root / 'run'
        data = self.summary(output, status='failed', cases=[dict(id='a', status='failed')])
        manifest = dict(schemaVersion=1, scope='batch-screenshot-review', runId='test-run',
            status='passed', cases=data['cases'], checkpoints=[dict(id='image-1', case='a', index=1,
            kind='verify.screenshot', automaticStatus='failed', comparisonStatus='different',
            reviewStatus='not_applicable', actual='image.png')])
        (output / 'screenshot_review.json').write_text(json.dumps(manifest))
        (output / 'image.png').write_bytes(b'image')
        identifier = self.w._register_run(output)
        result = self.w.result(identifier)
        self.assertEqual(result['status'], 'failed')
        self.assertEqual(result['cases'][1]['status'], 'not_run')
        self.assertIn('/api/artifact?', result['checkpoints'][0]['actualURL'])
        self.assertFalse(result['checkpoints'][0]['canReview'])
        self.assertEqual(json.loads((output / 'summary.json').read_text()), data)

    def test_latest_case_result_rejects_changed_binary_or_inputs(self):
        output = self.w.repository.playbacks / 'session_1'
        self.summary(output, cases=[dict(id='a', status='passed')])
        (output / 'a').mkdir()
        case = dict(status='passed', provenance=dict(applicationSha256='other', inputs=self.w.case_hashes['a']))
        case_file = output / 'a/case_result.json'
        case_file.write_text(json.dumps(case))
        self.assertIsNone(self.w.state()['cases'][0]['latestStatus'])
        case['provenance']['applicationSha256'] = self.w.application_hash
        case_file.write_text(json.dumps(case))
        self.assertEqual(self.w.state()['cases'][0]['latestStatus'], 'passed')
        case['provenance']['inputs'] = {'journal': 'changed'}
        case_file.write_text(json.dumps(case))
        self.assertIsNone(self.w.state()['cases'][0]['latestStatus'])

    def test_active_batch_keeps_upcoming_cases_waiting_in_result_view(self):
        output = self.root / 'run'
        data = self.summary(output, status='not_verified', cases=[dict(id='a', status='passed')])
        (output / 'screenshot_review.json').write_text(json.dumps(dict(schemaVersion=1,
            scope='batch-screenshot-review', runId='test-run', status='not_verified',
            cases=data['cases'], checkpoints=[])))
        identifier = self.w._register_run(output)
        self.w.task.update(state='paused', resultId=identifier, plannedCases=['a', 'b'])
        self.assertEqual(self.w.result(identifier)['cases'][1]['status'], 'waiting')
        self.w.task['state'] = 'cancelled'
        self.assertEqual(self.w.result(identifier)['cases'][1]['status'], 'not_run')

    def test_explicit_saved_result_can_be_opened_without_moving_or_rewriting_it(self):
        output = self.root / 'outside-default-output'
        data = self.summary(output, status='failed', cases=[dict(id='a', status='failed')])
        (output / 'screenshot_review.json').write_text(json.dumps(dict(schemaVersion=1,
            scope='batch-screenshot-review', runId='test-run', status='failed',
            cases=data['cases'], checkpoints=[])))
        before = (output / 'summary.json').read_bytes()
        identifier = self.w.open_result(dict(path=str(output)))['id']
        self.assertEqual(self.w.result(identifier)['status'], 'failed')
        self.assertEqual((output / 'summary.json').read_bytes(), before)
        self.assertEqual(self.w._history()[0]['id'], identifier)
        with self.assertRaises((WorkbenchError, FileNotFoundError)):
            self.w.open_result(dict(path=str(self.root)))


class TransportTests(unittest.TestCase):
    def setUp(self):
        WorkbenchTests.setUp(self)
        self.server = WorkbenchServer(('127.0.0.1', 0), self.w, '012345')
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.addCleanup(self.server.server_close)
        self.addCleanup(self.server.shutdown)

    def request(self, method, path, data=None, *, auth=False, headers=None):
        connection = http.client.HTTPConnection(*self.server.server_address, timeout=3)
        values = {'Content-Type': 'application/json'}
        if auth:
            values['Authorization'] = 'Bearer ' + self.server.session_token
        values.update(headers or {})
        connection.request(method, path, json.dumps(data) if data is not None else None, values)
        response = connection.getresponse()
        result = response.status, dict(response.headers), response.read()
        connection.close()
        return result

    def test_login_cookie_and_cross_origin_commands(self):
        self.assertEqual(self.request('GET', '/api/state')[0], 401)
        self.assertEqual(self.request('POST', '/api/login', dict(token='错误'))[0], 401)
        code, headers, body = self.request('POST', '/api/login', dict(token='012345'))
        self.assertEqual(code, 200)
        self.assertIn('HttpOnly', headers['Set-Cookie'])
        self.assertEqual(self.request('GET', '/api/state', headers={'Cookie': headers['Set-Cookie']})[0], 200)
        self.assertEqual(self.request('POST', '/api/config', dict(commandId='one'), auth=True,
            headers={'Origin': 'http://unrelated.example'})[0], 403)

    def test_short_code_cannot_authorize_api_without_login_and_cookie_survives_reload(self):
        self.assertEqual(self.request('GET', '/api/state', headers={'Authorization': 'Bearer 012345'})[0], 401)
        code, headers, _ = self.request('POST', '/api/login', dict(token='012345'))
        self.assertEqual(code, 200)
        cookie = headers['Set-Cookie']
        self.assertIn('HttpOnly', cookie)
        self.assertIn('SameSite=Strict', cookie)
        self.assertNotIn('012345', cookie)
        self.assertGreater(len(self.server.session_token), 30)
        for _ in range(2):
            self.assertEqual(self.request('GET', '/api/state', headers={'Cookie': cookie})[0], 200)

    def test_fifth_wrong_code_blocks_login_for_sixty_seconds_without_invalidating_session(self):
        _, headers, _ = self.request('POST', '/api/login', dict(token='012345'))
        cookie = headers['Set-Cookie']
        with patch('journal_server.time.monotonic', return_value=100) as clock:
            for _ in range(4):
                self.assertEqual(self.request('POST', '/api/login', dict(token='999999'))[0], 401)
            code, headers, _ = self.request('POST', '/api/login', dict(token='999999'))
            self.assertEqual(code, 429)
            self.assertEqual(headers['Retry-After'], '60')
            clock.return_value = 101
            code, headers, _ = self.request('POST', '/api/login', dict(token='012345'),
                headers={'X-Forwarded-For': '192.0.2.1'})
            self.assertEqual(code, 429)
            self.assertEqual(headers['Retry-After'], '59')
            self.assertEqual(self.request('GET', '/api/state', headers={'Cookie': cookie})[0], 200)
            self.assertEqual(self.server.login('012345', '192.0.2.1'), (True, 0))
            clock.return_value = 160
            self.assertEqual(self.request('POST', '/api/login', dict(token='012345'))[0], 200)

    def test_successful_login_resets_consecutive_failures(self):
        for _ in range(2):
            for _ in range(4):
                self.assertEqual(self.request('POST', '/api/login', dict(token='999999'))[0], 401)
            self.assertEqual(self.request('POST', '/api/login', dict(token='012345'))[0], 200)

    def test_duplicate_commands_are_not_executed_twice_and_mismatch_rejected(self):
        body = dict(commandId='same', application=str(self.program))
        with patch.object(self.w, 'configure') as configure:
            self.assertEqual(self.request('POST', '/api/config', body, auth=True)[0], 202)
            self.assertEqual(self.request('POST', '/api/config', body, auth=True)[0], 202)
            configure.assert_called_once()
            body['exports'] = 'different'
            self.assertEqual(self.request('POST', '/api/config', body, auth=True)[0], 409)

    def test_static_paths_and_artifact_registration(self):
        self.assertEqual(self.request('GET', '/../../requirements.txt')[0], 404)
        self.assertEqual(self.request('GET', '/api/artifact?run=bad&path=private.txt', auth=True)[0], 404)
        self.assertEqual(self.request('POST', '/api/config', {}, auth=True)[0], 400)


if __name__ == '__main__':
    unittest.main()
