#!/usr/bin/env python3
"""Serve the Journal workbench on the computer that runs GPlatform."""
import argparse
from collections import OrderedDict
from http import HTTPStatus
from http.cookies import SimpleCookie
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import math
import mimetypes
import os
from pathlib import Path
import re
import secrets
import socket
import sys
import threading
import time
from urllib.parse import parse_qs, urlsplit
import webbrowser

from journal_workbench import JournalWorkbench, WorkbenchError

WEB = Path(__file__).resolve().parent / 'web'


def connection_code(value=None):
    if value is None:
        return f'{secrets.randbelow(1_000_000):06d}'
    if not isinstance(value, str) or re.fullmatch(r'[0-9]{6}', value) is None:
        raise ValueError('连接码必须为6位数字')
    return value


class WorkbenchServer(ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self, address, workbench, token):
        self.workbench = workbench
        self.token = connection_code(token)
        self.session_token = secrets.token_urlsafe(32)
        self.auth_lock = threading.Lock()
        self.login_attempts = OrderedDict()
        self.command_lock = threading.Lock()
        self.commands = OrderedDict()
        super().__init__(address, WorkbenchHandler)

    def login(self, token, address):
        """Return (accepted, retry seconds); existing sessions remain independent."""
        with self.auth_lock:
            now = time.monotonic()
            failures, blocked_until = self.login_attempts.get(address, (0, 0))
            if blocked_until > now:
                return False, math.ceil(blocked_until - now)
            if blocked_until:
                failures = 0
            if (isinstance(token, str) and re.fullmatch(r'[0-9]{6}', token) is not None and
                    secrets.compare_digest(token, self.token)):
                self.login_attempts.pop(address, None)
                return True, 0
            failures += 1
            self.login_attempts[address] = (failures, now + 60 if failures >= 5 else 0)
            self.login_attempts.move_to_end(address)
            while len(self.login_attempts) > 1024:
                self.login_attempts.popitem(last=False)
            return False, 60 if failures >= 5 else 0


class WorkbenchHandler(BaseHTTPRequestHandler):
    server_version = 'JournalWorkbench/1'
    protocol_version = 'HTTP/1.1'

    def setup(self):
        super().setup()
        self.connection.settimeout(15)

    def log_message(self, format, *args):
        # Never log access codes, cookies or filesystem query parameters.
        if getattr(self, 'command', '') == 'GET' and urlsplit(getattr(self, 'path', '/')).path in ('/api/state', '/api/result'):
            return
        print(self.address_string(), getattr(self, 'command', 'request'),
            urlsplit(getattr(self, 'path', '/')).path, flush=True)

    def _send(self, code, body, content_type='application/json; charset=utf-8', headers=None):
        if not isinstance(body, bytes):
            body = json.dumps(body, ensure_ascii=False).encode('utf8')
        self.send_response(code)
        self.send_header('Content-Type', content_type)
        self.send_header('Content-Length', str(len(body)))
        self.send_header('Cache-Control', 'no-store')
        self.send_header('X-Content-Type-Options', 'nosniff')
        self.send_header('Referrer-Policy', 'no-referrer')
        self.send_header('Content-Security-Policy', "default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' blob:; connect-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'self'")
        for key, value in (headers or {}).items():
            self.send_header(key, value)
        self.end_headers()
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError):
            pass  # Closing a browser never cancels a machine-owned task.

    def _authorized(self):
        authorization = self.headers.get('Authorization', '')
        if authorization.startswith('Bearer ') and secrets.compare_digest(authorization[7:].encode(), self.server.session_token.encode()):
            return True
        cookie = SimpleCookie()
        try:
            cookie.load(self.headers.get('Cookie', ''))
            value = cookie.get('journal_session')
            return bool(value and secrets.compare_digest(value.value.encode(), self.server.session_token.encode()))
        except Exception:
            return False

    def _origin_allowed(self):
        origin = self.headers.get('Origin')
        return origin is None or origin == 'http://' + self.headers.get('Host', '')

    def do_GET(self):
        parsed = urlsplit(self.path)
        if not parsed.path.startswith('/api/'):
            files = {'/': ('index.html', 'text/html; charset=utf-8'),
                '/index.html': ('index.html', 'text/html; charset=utf-8'),
                '/app.js': ('app.js', 'text/javascript; charset=utf-8'),
                '/app.css': ('app.css', 'text/css; charset=utf-8')}
            entry = files.get(parsed.path)
            if entry is None:
                self._send(404, dict(error='页面不存在'))
                return
            try:
                self._send(200, (WEB / entry[0]).read_bytes(), entry[1])
            except OSError:
                self._send(503, dict(error='网页资源未安装'))
            return
        if not self._authorized():
            self._send(401, dict(error='请输入 A 电脑显示的6位连接码'))
            return
        args = parse_qs(parsed.query)
        value = lambda key: args.get(key, [''])[0]
        workbench = self.server.workbench
        try:
            if parsed.path == '/api/state':
                self._send(200, workbench.state())
            elif parsed.path == '/api/result':
                self._send(200, workbench.result(value('id')))
            elif parsed.path == '/api/artifact':
                path = workbench.artifact(value('run'), value('path'))
                image = path.suffix.lower() in ('.png', '.jpg', '.jpeg', '.webp')
                content_type = mimetypes.guess_type(path.name)[0] if image else 'application/octet-stream'
                self._send(200, path.read_bytes(), content_type or 'application/octet-stream',
                    None if image else {'Content-Disposition': 'attachment'})
            else:
                self._send(404, dict(error='接口不存在'))
        except FileNotFoundError as exc:
            self._send(404, dict(error=str(exc)))
        except (WorkbenchError, ValueError, KeyError, OSError) as exc:
            self._send(400, dict(error=str(exc)))

    def do_POST(self):
        if not self._origin_allowed():
            self._send(403, dict(error='请求来源与工作台不一致'))
            self.close_connection = True
            return
        try:
            length = int(self.headers.get('Content-Length', '0'))
            if not 0 < length <= 65536:
                raise WorkbenchError('请求大小无效')
            if self.headers.get_content_type() != 'application/json':
                raise WorkbenchError('请求必须为 JSON')
            data = json.loads(self.rfile.read(length))
            if not isinstance(data, dict):
                raise WorkbenchError('请求必须为 JSON 对象')
        except (ValueError, OSError) as exc:
            self._send(400, dict(error=str(exc)))
            self.close_connection = True
            return
        path = urlsplit(self.path).path
        if path == '/api/login':
            accepted, retry = self.server.login(data.get('token'), self.client_address[0])
            if retry:
                self._send(429, dict(error=f'尝试过多，请{retry}秒后重试'), headers={'Retry-After': str(retry)})
                return
            if not accepted:
                self._send(401, dict(error='连接码不正确，请输入6位数字'))
                return
            self._send(200, dict(ok=True), headers={'Set-Cookie':
                'journal_session=' + self.server.session_token + '; HttpOnly; SameSite=Strict; Path=/'})
            return
        if not self._authorized():
            self._send(401, dict(error='请先登录工作台'))
            return
        w = self.server.workbench
        operations = {'/api/config': lambda: w.configure(data), '/api/run': lambda: w.run(data),
            '/api/pause': w.pause, '/api/resume': w.resume, '/api/cancel': w.cancel,
            '/api/connect': lambda: w.connect(data), '/api/record/start': lambda: w.record_start(data),
            '/api/record/checkpoint': lambda: w.record_checkpoint(data),
            '/api/record/stop': lambda: w.record_stop(data),
            '/api/record/replay': lambda: w.record_replay(data), '/api/review': lambda: w.review(data),
            '/api/result/open': lambda: w.open_result(data)}
        if path not in operations:
            self._send(404, dict(error='接口不存在'))
            return
        command = data.get('commandId')
        if not isinstance(command, str) or not 1 <= len(command) <= 100:
            self._send(400, dict(error='缺少操作标识，请刷新网页'))
            return
        fingerprint = json.dumps([path, data], sort_keys=True, ensure_ascii=False)
        with self.server.command_lock:
            previous = self.server.commands.get(command)
            if previous:
                if previous[0] != fingerprint:
                    self._send(409, dict(error='操作标识已用于另一个请求'))
                else:
                    self._send(previous[1], previous[2])
                return
            try:
                result = operations[path]()
                response, code = dict(ok=True, **(result or {})), 202
            except (ValueError, OSError, KeyError, TypeError) as exc:
                response, code = dict(error=str(exc)), 409
            self.server.commands[command] = (fingerprint, code, response)
            while len(self.server.commands) > 256:
                self.server.commands.popitem(last=False)
            self._send(code, response)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--application', type=Path, help='A 电脑上的 GPlatform 可执行文件')
    parser.add_argument('--app-root', type=Path, help='包含 tests/TestAssets 的 App 源码目录')
    parser.add_argument('--exports', type=Path, help='A 电脑上的实际导出目录')
    parser.add_argument('--output-root', type=Path, help='回放结果保存目录')
    parser.add_argument('--listen', default='127.0.0.1', help='本机默认127.0.0.1；局域网访问使用0.0.0.0')
    parser.add_argument('--port', type=int, default=8765, help='网页服务端口；不同于 GPlatform 的 Journal 端口')
    parser.add_argument('--token', default=os.environ.get('GPLATFORM_WORKBENCH_TOKEN'), help='6位数字连接码，默认每次随机生成')
    parser.add_argument('--no-browser', action='store_true')
    args = parser.parse_args(argv)
    try:
        token = connection_code(args.token)
    except ValueError as exc:
        parser.error(str(exc))
    workbench = JournalWorkbench(args.application, args.app_root, args.exports, output_root=args.output_root)
    # Only one workbench may own this application/session repository at a time.
    lock_path = workbench.repository.recordings / '.workbench.lock'
    lock_path.parent.mkdir(parents=True, exist_ok=True)
    lock_file = lock_path.open('a+b')
    try:
        if os.name == 'nt':
            import msvcrt
            lock_file.seek(0)
            lock_file.write(b'0')
            lock_file.flush()
            lock_file.seek(0)
            msvcrt.locking(lock_file.fileno(), msvcrt.LK_NBLCK, 1)
        else:
            import fcntl
            fcntl.flock(lock_file, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except OSError:
        parser.error('此电脑已经启动 Journal 工作台，请使用现有服务')
    server = WorkbenchServer((args.listen, args.port), workbench, token)
    port = server.server_address[1]
    local = f'http://{args.listen if args.listen not in ("0.0.0.0", "") else "127.0.0.1"}:{port}'
    print('Journal 工作台（执行电脑：' + socket.gethostname() + '）', flush=True)
    print('本机访问：' + local, flush=True)
    if args.listen != '127.0.0.1':
        try:
            addresses = sorted({item[4][0] for item in socket.getaddrinfo(socket.gethostname(), port,
                socket.AF_INET, socket.SOCK_STREAM) if not item[4][0].startswith('127.')})
        except OSError:
            addresses = []
        for address in addresses:
            print(f'其他电脑访问：http://{address}:{port}', flush=True)
        if not addresses:
            print(f'其他电脑访问：http://<此电脑局域网IP>:{port}', flush=True)
    print('连接码：' + token, flush=True)
    print('关闭网页不会停止任务。结束录制后按 Ctrl+C 停止服务。', flush=True)
    if not args.no_browser:
        webbrowser.open(local)
    while True:
        try:
            server.serve_forever(poll_interval=.25)
        except KeyboardInterrupt:
            try:
                workbench.close()
            except WorkbenchError as exc:
                print(str(exc), flush=True)
                continue
            break
    server.server_close()
    lock_file.close()


if __name__ == '__main__':
    main()
