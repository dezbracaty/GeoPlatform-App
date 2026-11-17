"""Exercise real HTTP/login/owned Server shutdown without launching a product App."""
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'scripts/ci'))
from mac_regression import server_regression

SERVER = r'''
import argparse,json,sys
from pathlib import Path
sys.path.insert(0,sys.argv[1]);del sys.argv[1]
from journal_server import WorkbenchServer
p=argparse.ArgumentParser()
for name in ('application','app-root','assets','output-root','exports','listen','token'):
    p.add_argument('--'+name)
p.add_argument('--port',type=int);p.add_argument('--no-browser',action='store_true')
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
        if mode=='candidate':summary['candidateCollection']=True
        if mode=='partial':summary['cases'].pop()
        if mode=='cleanup':summary['cleanupError']='still alive'
        (self.directory/'summary.json').write_text(json.dumps(summary))
    def state(self):
        self.reads+=1
        # An early all-green summary is insufficient: serial stage is still running.
        if self.reads>=2 and self.task['state']=='running' and mode!='cancel':self.task['state']='completed'
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
        self.real=Path(__file__).resolve().parents[2]/'tools/journal'
        shutil.copyfile(self.real/'session_repository.py',self.tools/'session_repository.py')
        # The real existing HTTP handler is used; the fake worker only produces fixture results.
        (self.tools/'journal_server.py').write_text(SERVER.replace('sys.argv[1]);del sys.argv[1]',repr(str(self.real))+')'))
        for name in ['one','two']:
            path=self.source/'tests/TestAssets/scenarios'/name/'events';path.mkdir(parents=True)
            (path/'Journal.json').write_text('{}')
        self.output=self.root/'evidence';self.output.mkdir()

    def run_fixture(self,mode,timeout=10):
        (self.source/'mode').write_text(mode)
        return server_regression(self.source,self.root/'unused-app',self.output,dict(os.environ),timeout=timeout)

    def test_login_all_inventory_waits_for_terminal_task_and_closes_owned_server(self):
        summary,directory=self.run_fixture('passed')
        self.assertEqual(summary['completedCaseCount'],2)
        self.assertEqual(directory,self.output/'run/session_fixture')
        state=json.loads((self.output/'server-state.json').read_text())
        self.assertGreaterEqual(state['reads'],2)
        self.assertTrue((self.output/'run/closed').is_file())

    def test_candidate_partial_and_cleanup_failure_cannot_pass(self):
        for mode in ['candidate','partial','cleanup']:
            with self.subTest(mode=mode):
                self.output=self.root/mode;self.output.mkdir()
                with self.assertRaisesRegex(RuntimeError,'did not pass'):
                    self.run_fixture(mode)
                self.assertTrue((self.output/'run/closed').is_file())

    def test_timeout_cancels_and_waits_before_closing_owned_server(self):
        with self.assertRaisesRegex(TimeoutError,'budget exhausted'):
            self.run_fixture('cancel',timeout=1.5)
        self.assertTrue((self.output/'run/cancelled').is_file())
        self.assertTrue((self.output/'run/closed').is_file())
