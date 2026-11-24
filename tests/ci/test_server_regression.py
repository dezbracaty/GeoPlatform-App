"""Exercise real HTTP/login/owned Server shutdown without launching a product App."""
from contextlib import redirect_stdout
import io
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
from unittest.mock import patch
from urllib.request import OpenerDirector

sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'scripts/ci'))
import mac_regression
from mac_regression import server_regression

SERVER = r'''
import ci_server_sibling_fixture
import argparse,json,sys,time
from pathlib import Path
sys.path.insert(0,sys.argv[1]);del sys.argv[1]
from journal_server import WorkbenchServer
p=argparse.ArgumentParser()
for name in ('application','app-root','assets','output-root','exports','listen','token'):
    p.add_argument('--'+name)
p.add_argument('--port',type=int);p.add_argument('--no-browser',action='store_true')
p.add_argument('--parallelism',type=int)
a=p.parse_args()
root=Path(a.output_root);mode=Path(a.app_root,'mode').read_text();root.mkdir()
class Workbench:
    def __init__(self): self.reads=0;self.task={'state':'idle'}
    def run(self,data):
        if data['cases'] != ['one','two']:raise ValueError('Wrong fixed inventory')
        self.directory=root/'session_fixture';self.directory.mkdir()
        self.task={'id':'owned-task','state':'running','output':str(self.directory)}
        summary={'status':'passed','runComplete':True,'selectionScope':'all',
                 'plannedCases':data['cases'],'completedCaseCount':2,
                 'cases':[{'id':x,'status':'passed'} for x in data['cases']]}
        if a.parallelism is not None:summary['parallelism']=a.parallelism
        if mode=='candidate':summary['candidateCollection']=True
        if mode=='partial':summary['cases'].pop()
        if mode=='cleanup':summary['cleanupError']='still alive'
        (self.directory/'summary.json').write_text(json.dumps(summary))
    def state(self):
        self.reads+=1
        if self.reads == 1 and mode in ('slow-state', 'cancel-slow-state'):
            time.sleep(.75)
        # An early all-green summary is insufficient: serial stage is still running.
        if mode=='progress':
            self.task.update(case='one' if self.reads==1 else 'two', step=self.reads, total=2,
                stage='initial' if self.reads<3 else 'serial-recheck',
                rows=[{'status':'passed'},{'status':'running' if self.reads<4 else 'passed'}])
            if self.reads>=4:self.task['state']='completed'
        elif self.reads>=2 and self.task['state']=='running' and mode not in ('cancel', 'cancel-slow-state'):self.task['state']='completed'
        return {'task':self.task,'reads':self.reads}
    def pause(self): pass
    def resume(self): pass
    def cancel(self):
        self.task['state']='cancelled';(root/'cancelled').write_text('owned')
w=Workbench();s=WorkbenchServer((a.listen,a.port),w,a.token)
print('http://127.0.0.1:'+str(s.server_address[1]),flush=True)
try:s.serve_forever(poll_interval=.05)
except KeyboardInterrupt:pass
finally:s.server_close();(root/'closed').write_text('owned')
'''

class ServerRegressionTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name).resolve();self.source=self.root/'App';self.tools=self.source/'tools/journal'
        self.tools.mkdir(parents=True)
        (self.tools / 'ci_server_sibling_fixture.py').write_text('VALUE = \"script sibling\"\n')
        self.real=Path(__file__).resolve().parents[2]/'tools/journal'
        shutil.copyfile(self.real/'session_repository.py',self.tools/'session_repository.py')
        # The real existing HTTP handler is used; the fake worker only produces fixture results.
        (self.tools/'journal_server.py').write_text(SERVER.replace('sys.argv[1]);del sys.argv[1]',repr(str(self.real))+')'))
        for name in ['one','two']:
            path=self.source/'tests/TestAssets/scenarios'/name/'events';path.mkdir(parents=True)
            (path/'Journal.json').write_text('{}')
        self.output=self.root/'evidence';self.output.mkdir()

    def run_fixture(self,mode,timeout=10,parallelism=None):
        (self.source/'mode').write_text(mode)
        return server_regression(self.source,self.root/'unused-app',self.output,dict(os.environ),
                                 timeout=timeout,parallelism=parallelism)

    def test_login_all_inventory_waits_for_terminal_task_and_closes_owned_server(self):
        summary,directory=self.run_fixture('passed')
        self.assertEqual(summary['completedCaseCount'],2)
        self.assertEqual(directory,self.output/'run/session_fixture')
        state=json.loads((self.output/'server-state.json').read_text())
        self.assertGreaterEqual(state['reads'],2)
        self.assertTrue((self.output/'run/closed').is_file())

    def test_forward_explicit_serial_parallelism_to_owned_server(self):
        summary, _ = self.run_fixture('passed', parallelism=1)
        self.assertEqual(summary['parallelism'], 1)
        self.assertEqual(summary['completedCaseCount'], 2)

    def test_progress_prints_stage_changes_and_terminal_counts_without_each_case(self):
        log = io.StringIO()
        with redirect_stdout(log):
            self.run_fixture('progress')
        progress = [json.loads(line.removeprefix('Journal ')) for line in log.getvalue().splitlines()
                    if line.startswith('Journal {')]
        self.assertEqual([(item['state'], item['stage'], item['passed']) for item in progress],
                         [('running', 'initial', 1), ('running', 'serial-recheck', 1),
                          ('completed', 'serial-recheck', 2)])
        self.assertNotIn('case', progress[0])
        self.assertNotIn('step', progress[0])
        state = json.loads((self.output / 'server-state.json').read_text())
        self.assertEqual(state['task']['step'], 4)

    def test_candidate_partial_and_cleanup_failure_cannot_pass(self):
        for mode in ['candidate','partial','cleanup']:
            with self.subTest(mode=mode):
                self.output=self.root/mode;self.output.mkdir()
                with self.assertRaisesRegex(RuntimeError,'did not pass'):
                    self.run_fixture(mode)
                self.assertTrue((self.output/'run/closed').is_file())

    def slow_first_state_request(self, timeout=.15):
        original = OpenerDirector.open
        seen = False

        def open_with_short_first_timeout(opener, request, *args, **kwargs):
            nonlocal seen
            if request.full_url.endswith('/api/state') and not seen:
                seen = True
                kwargs['timeout'] = timeout
            return original(opener, request, *args, **kwargs)

        return patch.object(OpenerDirector, 'open', open_with_short_first_timeout)

    def test_slow_state_recovers_without_cancelling(self):
        log = io.StringIO()
        with self.slow_first_state_request(), redirect_stdout(log):
            summary, directory = self.run_fixture('slow-state')
        self.assertEqual(summary['completedCaseCount'], 2)
        self.assertIn('Journal Server state request timed out', log.getvalue())
        self.assertFalse((directory.parent / 'cancelled').exists())
        self.assertTrue((directory.parent / 'closed').is_file())

    def test_state_recovery_budget_exhaustion_still_cleans_up(self):
        with self.slow_first_state_request(.55), patch.object(mac_regression, 'STATE_RECOVERY_SECONDS', .4):
            with self.assertRaisesRegex(TimeoutError, 'state did not respond within the recovery budget'):
                self.run_fixture('cancel-slow-state')
        self.assertTrue((self.output/'run/cancelled').is_file())
        self.assertTrue((self.output/'run/closed').is_file())

    def test_timeout_cancels_and_waits_before_closing_owned_server(self):
        with self.assertRaisesRegex(TimeoutError,'budget exhausted'):
            self.run_fixture('cancel',timeout=1.5)
        self.assertTrue((self.output/'run/cancelled').is_file())
        self.assertTrue((self.output/'run/closed').is_file())
