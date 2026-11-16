"""Machine-local task owner shared by every browser connected to the workbench."""
import copy
import hashlib
import json
from pathlib import Path
import socket
import sys
import tempfile
import threading
import time
from urllib.parse import urlencode
import uuid

TOOLS = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS.parent))
from journalsystem import JournalSystem
from journal_runner import JournalRunner, load_journal
from journal_execution_session import JournalExecutionSession
from session_repository import JournalSessionRepository
from review_model import ReviewBatch
import run_acceptance


class WorkbenchError(ValueError):
    pass


class JournalWorkbench:
    def __init__(self, application=None, app_root=None, exports=None, *, output_root=None):
        self.app_root = Path(app_root or TOOLS.parents[1]).resolve()
        self.assets = self.app_root / 'tests/TestAssets'
        self.application = str(Path(application).resolve()) if application else ''
        self.exports = str(Path(exports).resolve()) if exports else ''
        defaults = JournalSessionRepository.default_paths(self.application or '.')
        self.output_root = Path(output_root).resolve() if output_root else None
        self.repository = JournalSessionRepository(recordings=defaults['recordings'],
            playbacks=self.output_root or defaults['playbacks'], scenarios=self.assets / 'scenarios')
        self.lock = threading.RLock()
        self.condition = threading.Condition(self.lock)
        self.cancel_event = threading.Event()
        self.worker = None
        self.client = self.runner = None
        self.connection_error = ''
        self.record_port = 9999
        self.record_name = ''
        self.record_started = None
        self.record_points = []
        self.record_ids = {}
        self.run_ids = {}
        self.cases = []
        self.case_hashes = {}
        self.application_hash = ''
        self.pause_requested = False
        self.closing = False
        self.task = dict(id=None, kind=None, state='idle', rows=[], logs=[], plannedCases=[])
        self.load_cases()

    @staticmethod
    def identity(path):
        return hashlib.sha256(str(Path(path).resolve()).encode()).hexdigest()[:24]

    def load_cases(self):
        cases, hashes = [], {}
        for item in self.repository.list_sessions():
            if item['type'] != '场景':
                continue
            name = item['path'].name
            row = dict(id=name, name=item['name'], description=item['metadata'].get('acceptanceTarget', ''),
                error=item.get('error'), checkpointCount=0, method='', latestStatus=None,
                needsExports=bool(item['metadata'].get('artifacts')))
            try:
                case = run_acceptance.load_case(self.assets, name, load_journal)
                methods = case['metadata'].get('verification', {}).get('requiredMethods', [])
                row['method'] = ' + '.join({'db': 'DB', 'screenshot': '截图'}.get(m, m) for m in methods)
                row['checkpointCount'] = len(case['verificationPlan']['checks'])
                hashes[name] = case['hashes']
            except Exception as exc:
                row['error'] = str(exc)
            cases.append(row)
        with self.lock:
            self.cases, self.case_hashes = sorted(cases, key=lambda c: c['id']), hashes
            if self.application and Path(self.application).is_file():
                self.application_hash = hashlib.sha256(Path(self.application).read_bytes()).hexdigest()

    def _active_recording(self):
        return bool(self.runner and (self.runner.recording or self.runner.has_pending_save))

    def _busy(self):
        return self.task['state'] in ('running', 'paused', 'cancelling')

    def _idle(self, *, allow_record=False):
        if self.closing:
            raise WorkbenchError('工作台正在关闭')
        if self._busy():
            raise WorkbenchError('当前任务尚未结束，请先停止或等待完成')
        if self._active_recording() and not allow_record:
            raise WorkbenchError('请先停止并保存当前录制')

    def log(self, message):
        with self.lock:
            self.task['logs'].append(time.strftime('%H:%M:%S') + '  ' + str(message))
            self.task['logs'] = self.task['logs'][-1000:]

    def _submit(self, kind, operation, *, allow_record=False, **fields):
        with self.lock:
            self._idle(allow_record=allow_record)
            self.cancel_event.clear()
            self.pause_requested = False
            self.task = dict(id=uuid.uuid4().hex, kind=kind, state='running', rows=[], logs=[],
                plannedCases=[], caseIndex=0, step=0, totalSteps=0, error=None)
            self.task.update(fields)
            def run():
                try:
                    operation()
                    with self.lock:
                        if self.task['state'] not in ('failed', 'cancelled'):
                            self.task['state'] = 'cancelled' if self.cancel_event.is_set() else 'completed'
                except Exception as exc:
                    with self.lock:
                        self.task.update(state='cancelled' if self.cancel_event.is_set() else 'failed', error=str(exc))
                    self.log(str(exc))
                finally:
                    with self.condition:
                        self.condition.notify_all()
            self.worker = threading.Thread(target=run, name='journal-task', daemon=False)
            self.worker.start()

    def configure(self, data):
        with self.lock:
            self._idle()
            application = data.get('application', self.application)
            exports = data.get('exports', self.exports)
            if not isinstance(application, str) or not isinstance(exports, str):
                raise WorkbenchError('程序和导出目录必须是 A 电脑上的路径')
            if application and not Path(application).is_file():
                raise WorkbenchError('程序路径必须指向 A 电脑上实际的可执行文件')
            if exports and not Path(exports).is_dir():
                raise WorkbenchError('导出目录不存在')
            self.application = str(Path(application).resolve()) if application else ''
            self.exports = str(Path(exports).resolve()) if exports else ''
            self.application_hash = hashlib.sha256(Path(application).read_bytes()).hexdigest() if application else ''
            if self.output_root is None:
                self.repository.playbacks = JournalSessionRepository.default_paths(application or '.')['playbacks']

    def _register_run(self, directory):
        directory = Path(directory).resolve()
        identifier = self.identity(directory)
        with self.lock:
            self.run_ids[identifier] = directory
        return identifier

    def _history(self):
        rows = []
        for path in self.repository.result_directories():
            try:
                summary_file = path / 'summary.json'
                summary = json.loads((summary_file if summary_file.is_file() else path / 'execution_result.json').read_text(encoding='utf8'))
                if not self.application or Path(summary.get('applicationPath', '')).resolve() != Path(self.application):
                    continue
                if summary_file.is_file():
                    if Path(summary.get('assetsRoot', '')).resolve() != self.assets:
                        continue
                else:
                    journal = Path(summary.get('journalPath', '')).resolve()
                    if not (journal.is_relative_to(self.assets / 'scenarios') or journal.is_relative_to(self.repository.recordings.resolve())):
                        continue
                rows.append(dict(id=self._register_run(path), name=path.name,
                    status=summary.get('status', summary.get('acceptance', 'not_verified')),
                    createdAt=(summary_file if summary_file.is_file() else path / 'execution_result.json').stat().st_mtime,
                    plannedCases=summary.get('plannedCases', []),
                    completedCaseCount=summary.get('completedCaseCount', 0),
                    plannedCaseCount=summary.get('plannedCaseCount', 1)))
            except (OSError, ValueError, TypeError):
                continue
        return sorted(rows, key=lambda r: r['createdAt'], reverse=True)[:100]

    def _records(self):
        rows, registry = [], {}
        for item in JournalSessionRepository(recordings=self.repository.recordings).list_sessions():
            if not item['journal_file']:
                continue
            identifier = self.identity(item['path'])
            registry[identifier] = item
            count = 0
            error = item.get('error')
            try:
                document = load_journal(item['journal_file'])
                count = sum(entry['kind'].startswith('verify.') for entry in document['entries'])
            except Exception as exc:
                error = str(exc)
            rows.append(dict(id=identifier, name=item['name'], mtime=item['mtime'],
                checkpointCount=count, error=error))
        with self.lock:
            self.record_ids = registry
        return rows

    def _task_rows(self, task):
        planned = task.get('plannedCases', [])
        by_id = {}
        if task.get('resultId'):
            directory = self.run_ids.get(task['resultId'])
            try:
                summary = json.loads((directory / 'summary.json').read_text(encoding='utf8'))
                by_id = {c['id']: c for c in summary['cases']}
                task['summary'] = summary
            except (OSError, ValueError, TypeError):
                pass
        task['rows'] = []
        for name in planned:
            status = by_id.get(name, {}).get('status')
            if status is None:
                status = ('running' if name == task.get('case') and task['state'] in ('running', 'cancelling') else
                    'not_run' if task['state'] in ('completed', 'cancelled', 'failed') else 'waiting')
            task['rows'].append(dict(id=name, status=status))

    def state(self):
        history, records = self._history(), self._records()
        with self.lock:
            task = copy.deepcopy(self.task)
            self._task_rows(task)
            cases = copy.deepcopy(self.cases)
            # A displayed latest pass must belong to the current binary and case inputs.
            for batch in history:
                directory = self.run_ids[batch['id']]
                for case in cases:
                    if case['latestStatus'] is not None:
                        continue
                    try:
                        result = json.loads((directory / case['id'] / 'case_result.json').read_text(encoding='utf8'))
                        provenance = result['provenance']
                        if (provenance.get('applicationSha256') == self.application_hash and
                                provenance.get('inputs') == self.case_hashes.get(case['id'])):
                            case['latestStatus'] = result['status']
                            batch_summary = json.loads((directory / 'summary.json').read_text(encoding='utf8'))
                            if batch_summary.get('cleanupError'):
                                case.update(latestStatus='failed', latestError=batch_summary['cleanupError'])
                    except (OSError, ValueError, KeyError):
                        continue
            return dict(config=dict(application=self.application, exports=self.exports,
                defaultExports=tempfile.gettempdir(),
                assetsRoot=str(self.assets), machine=socket.gethostname(), recordPort=self.record_port),
                cases=cases, task=task, records=records, history=history,
                recording=dict(connected=bool(self.client and self.client.client),
                    active=bool(self.runner and self.runner.recording),
                    pendingSave=bool(self.runner and self.runner.has_pending_save),
                    name=self.record_name, startedAt=self.record_started,
                    sessionId=self.identity(self.runner.session_path) if self.runner and self.runner.session_path else None,
                    checkpoints=copy.deepcopy(self.record_points), connectionError=self.connection_error))

    def _session(self):
        def progress(index, total, entry):
            with self.lock:
                self.task.update(step=index, totalSteps=total, stepKind=entry['kind'])
        return JournalExecutionSession(cancel_event=self.cancel_event, progress=progress, log=self.log)

    def run(self, data):
        ids = data.get('cases')
        known = {c['id']: c for c in self.cases}
        if not isinstance(ids, list) or not ids or any(not isinstance(c, str) or c not in known for c in ids):
            raise WorkbenchError('请选择要回放的场景')
        run_acceptance.select_cases(list(known), ','.join(ids))
        if any(known[c]['error'] for c in ids):
            raise WorkbenchError('所选场景准备阻塞，请先查看场景详情')
        if not self.application or not Path(self.application).is_file():
            raise WorkbenchError('请先配置 A 电脑的 GPlatform 可执行文件')
        needs_exports = any(known[c]['needsExports'] for c in ids)
        exports = self.exports
        pause_each = data.get('pauseAfterEach', False)
        if not isinstance(pause_each, bool):
            raise WorkbenchError('pauseAfterEach 必须为布尔值')
        output = self.repository.playbacks / ('session_' + time.strftime('%Y%m%d_%H%M%S') + '_' + uuid.uuid4().hex[:8])
        identifier = self._register_run(output)
        args = ['--assets', str(self.assets), '--application', self.application,
            '--cases', ','.join(ids), '--output', str(output)]
        def progress(event):
            with self.lock:
                self.task.update(case=event['case'], caseIndex=event['index'], total=event['total'])
                if event['phase'] == 'begin':
                    self.task.update(step=0, totalSteps=0)
            self.log(event['phase'] + ' ' + event['case'] + ' ' + event.get('status', ''))
        def boundary(refresh):
            with self.condition:
                if not (pause_each or self.pause_requested) or self.cancel_event.is_set():
                    return
                self.pause_requested = False
                self.task['state'] = 'paused'
            # App cleanup has completed. Pixel workers continue while the user inspects.
            while True:
                refresh()
                with self.condition:
                    if self.task['state'] != 'paused' or self.cancel_event.is_set():
                        return
                    self.condition.wait(timeout=.25)
        def operation():
            export_directory = exports
            if needs_exports and not export_directory:
                export_directory = tempfile.mkdtemp(prefix='gplatform-journal-exports-')
            run_args = args + ['--exports', export_directory] if export_directory else args
            if export_directory:
                self.log('本次导出目录：' + export_directory)
            code = run_acceptance.main(run_args, session=self._session(), progress=progress, between_cases=boundary)
            summary = json.loads((output / 'summary.json').read_text(encoding='utf8'))
            with self.lock:
                self.task['summary'] = summary
                self.task['state'] = ('cancelled' if self.cancel_event.is_set() else 'completed' if code == 0 else 'failed')
                self.task['error'] = summary.get('cleanupError') or summary.get('error')
        self._submit('regression', operation, plannedCases=ids, total=len(ids),
            resultId=identifier, output=str(output), pauseAfterEach=pause_each)

    def pause(self):
        with self.lock:
            if self.task['kind'] != 'regression' or self.task['state'] != 'running':
                raise WorkbenchError('当前没有可暂停的场景回放')
            self.pause_requested = True
            self.task['pauseRequested'] = True

    def resume(self):
        with self.condition:
            if self.task['state'] != 'paused':
                raise WorkbenchError('当前任务没有暂停')
            self.task.update(state='running', pauseRequested=False)
            self.condition.notify_all()

    def cancel(self):
        with self.condition:
            if self.task['kind'] not in ('regression', 'replay') or not self._busy():
                raise WorkbenchError('当前没有可停止的回放')
            self.cancel_event.set()
            self.task['state'] = 'cancelling'
            self.condition.notify_all()

    def connect(self, data):
        port = data.get('port', self.record_port)
        if isinstance(port, bool) or not isinstance(port, int) or not 1 <= port <= 65535:
            raise WorkbenchError('端口必须在 1–65535 之间')
        def operation():
            client = JournalSystem(request_timeout=10)
            try:
                client.connect('127.0.0.1', port)
                client.version()
                paths = client.paths()
                with self.lock:
                    runner = self.runner or JournalRunner(client)
                    runner.reconnect(client)
                    previous = self.client
                    self.client, self.runner = client, runner
                    self.record_port = port
                    self.repository.recordings = Path(paths['recordings']).resolve()
                    self.connection_error = ''
                if previous:
                    previous.disconnect()
            except Exception as exc:
                client.disconnect()
                with self.lock:
                    if self.client:
                        self.client.disconnect()
                    self.client = None
                    self.connection_error = str(exc)
                raise
        self._submit('connect', operation, allow_record=True)

    def _require_connection(self):
        if not self.client or not self.client.client or not self.runner:
            raise WorkbenchError('请先连接 A 电脑上已打开的 GPlatform')

    def record_start(self, data):
        name = data.get('name', '')
        if not isinstance(name, str) or len(name) > 200:
            raise WorkbenchError('录制名称不能超过200个字符')
        with self.lock:
            self._require_connection()
        def operation():
            path = self.runner.start_recording()
            with self.lock:
                self.record_name = name.strip() or path.name
                self.record_started = time.time()
                self.record_points = []
            self.log('开始录制：' + str(path))
        self._submit('record-start', operation)

    def record_checkpoint(self, data):
        kind, label = data.get('kind'), data.get('label', '')
        if kind not in ('screenshot', 'db') or not isinstance(label, str) or len(label) > 1000:
            raise WorkbenchError('检查点类型或备注无效')
        identifier = JournalSystem.normalize_db_id(data.get('dbInstanceId', '')) if kind == 'db' else None
        with self.lock:
            self._require_connection()
            if not self.runner.recording:
                raise WorkbenchError('请先开始录制')
        def operation():
            entry = self.runner.record_db(identifier, label) if kind == 'db' else self.runner.record_screenshot(label or 'checkpoint')
            with self.lock:
                self.record_points.append(dict(kind=kind, label=label, atMs=entry['atMs']))
            self.log('采集候选检查点：' + kind)
        self._submit('record-checkpoint', operation, allow_record=True)

    def record_stop(self, data):
        with self.lock:
            if not (self.runner and self.runner.has_pending_save):
                self._require_connection()
            if not self._active_recording():
                raise WorkbenchError('当前没有待保存录制')
            name = data.get('name', self.record_name)
            if not isinstance(name, str) or len(name) > 200:
                raise WorkbenchError('录制名称无效')
        def operation():
            journal = self.runner.stop_recording({'name': name})
            self.log('录制已保存，候选尚未批准为标准：' + str(journal))
        self._submit('record-stop', operation, allow_record=True)

    def record_replay(self, data):
        self._records()
        record = self.record_ids.get(data.get('id'))
        if not record or record.get('error'):
            raise WorkbenchError('录制不存在或不可读取')
        if not self.application or not Path(self.application).is_file():
            raise WorkbenchError('请先配置 A 电脑的 GPlatform 可执行文件')
        output = self.repository.playbacks / ('session_' + time.strftime('%Y%m%d_%H%M%S') + '_' + uuid.uuid4().hex[:8])
        identifier = self._register_run(output)
        def operation():
            result = self._session().run(record['journal_file'], application=self.application,
                output_dir=output, startup_timeout=120)
            self.repository.remember_result(output)
            with self.lock:
                self.task.update(state='cancelled' if self.cancel_event.is_set() else
                    'failed' if result.get('acceptance') == 'failed' else 'completed',
                    error=result.get('error'), diagnostic=True)
        name = record['journal_file'].parent.parent.name
        self._submit('replay', operation, resultId=identifier, output=str(output),
            recordId=data['id'], recordName=record['name'], diagnostic=True,
            plannedCases=[name], total=1, case=name, caseIndex=1)

    def result_directory(self, identifier):
        with self.lock:
            path = self.run_ids.get(identifier)
        if path is None:
            self._history()
            with self.lock:
                path = self.run_ids.get(identifier)
        if path is None:
            raise FileNotFoundError('结果不存在')
        return path

    def open_result(self, data):
        value = data.get('path')
        if not isinstance(value, str) or not value.strip():
            raise WorkbenchError('请输入 A 电脑上的结果目录')
        directory = Path(value).expanduser().resolve()
        if not directory.is_dir():
            raise WorkbenchError('结果目录不存在')
        # Validate the same maintained evidence model used by automatic history.
        # This is an explicit view operation and does not grant acceptance.
        ReviewBatch(directory)
        identifier = self._register_run(directory)
        self.repository.remember_result(directory)
        return dict(id=identifier)

    def artifact(self, identifier, relative):
        root = self.result_directory(identifier)
        if not isinstance(relative, str) or '\\' in relative or Path(relative).is_absolute():
            raise WorkbenchError('证据路径无效')
        path = (root / relative).resolve()
        if not path.is_relative_to(root) or not path.is_file():
            raise FileNotFoundError('证据不存在')
        return path

    def _url(self, identifier, root, path):
        path = Path(path).resolve()
        if not path.is_relative_to(root) or not path.is_file():
            return None
        return '/api/artifact?' + urlencode(dict(run=identifier, path=path.relative_to(root).as_posix()))

    def result(self, identifier):
        root = self.result_directory(identifier)
        with self.lock:
            task = copy.deepcopy(self.task)
        is_current = task.get('resultId') == identifier
        summary_file = root / 'summary.json'
        summary = None
        try:
            if summary_file.is_file():
                summary = json.loads(summary_file.read_text(encoding='utf8'))
            model = ReviewBatch(root)
        except (OSError, ValueError, KeyError):
            if not is_current or task['state'] not in ('running', 'paused', 'cancelling'):
                raise WorkbenchError('结果报告不完整或尚未生成')
            model = None
        rows, cases = [], []
        if model:
            cases = model.document.get('cases', [])
            for row in model.rows:
                item = copy.deepcopy(row)
                for key in ('expected', 'actual', 'diff', 'rawDiff'):
                    path = model.path(row, key)
                    item[key + 'URL'] = self._url(identifier, root, path) if path else None
                item.update(canReview=model.can_review(row), decision=model.decision(row))
                rows.append(item)
        directories = [(c['id'], root / c['id']) for c in cases] if summary is not None else [(cases[0]['id'] if cases else root.name, root)]
        for case_name, directory in directories:
            try:
                execution = json.loads((directory / 'execution_result.json').read_text(encoding='utf8'))
                for step in execution['results']:
                    if step.get('kind') != 'verify.db':
                        continue
                    evidence = []
                    details = {}
                    for value in step.get('evidence', []):
                        path = (directory / value).resolve()
                        url = self._url(identifier, root, path)
                        if url:
                            evidence.append(dict(name=path.name, url=url))
                            if path.suffix == '.json':
                                details[path.name] = json.loads(path.read_text(encoding='utf8'))
                    rows.append(dict(id=case_name + ':db:' + str(step['index']), case=case_name,
                        index=step['index'], label='DB · ' + str(step['index']), kind='verify.db',
                        automaticStatus=step['status'], comparisonStatus=step['status'],
                        message=step.get('message', ''), details=details, evidence=evidence, canReview=False))
                checks_file = directory / 'checks.json'
                if checks_file.is_file():
                    for check in json.loads(checks_file.read_text(encoding='utf8')):
                        if not check.get('supplemental'):
                            continue
                        rows.append(dict(id=case_name + ':check:' + check['id'], case=case_name,
                            label=check['id'], kind='artifact', automaticStatus=check['status'],
                            message=check.get('message', ''), details=check, canReview=False,
                            evidence=[dict(name=Path(p).name, url=self._url(identifier, root, directory / p))
                                for p in check.get('evidence', [])]))
            except (OSError, ValueError, KeyError):
                continue
        status = summary.get('status') if summary else model.document['status'] if model else 'pending'
        if not cases and is_current:
            self._task_rows(task)
            cases = task['rows']
        if summary:
            prior = {c['id']: c for c in cases}
            cases = [prior.get(name, dict(id=name, status='not_run')) for name in summary['plannedCases']]
        if is_current and task['state'] in ('running', 'paused', 'cancelling'):
            self._task_rows(task)
            live = {c['id']: c for c in task['rows']}
            cases = [dict(c, status=live[c['id']]['status'])
                if c.get('status') == 'not_run' and c['id'] in live else c for c in cases]
        error = (summary or {}).get('cleanupError') or (summary or {}).get('error')
        if is_current:
            error = error or task.get('error')
        return dict(id=identifier, status=status, cases=cases, checkpoints=rows,
            summary=summary, diagnostic=summary is None,
            error=error,
            logs=task.get('logs', []) if is_current else [], outputPath=str(root))

    def review(self, data):
        model = ReviewBatch(self.result_directory(data.get('run')))
        row = next((r for r in model.rows if r['id'] == data.get('id')), None)
        if row is None:
            raise WorkbenchError('检查点不存在')
        model.save(row, data.get('decision'), data.get('note', ''))

    def close(self):
        with self.lock:
            if self._active_recording():
                raise WorkbenchError('录制尚未保存，请先在网页停止并保存后关闭服务')
            self.closing = True
            if self._busy():
                self.cancel_event.set()
                with self.condition:
                    self.task['state'] = 'cancelling'
                    self.condition.notify_all()
            worker = self.worker
        if worker:
            worker.join()
        with self.lock:
            if self._active_recording():
                self.closing = False
                raise WorkbenchError('录制尚未保存，请先在网页停止并保存后关闭服务')
        if self.client:
            self.client.disconnect()
