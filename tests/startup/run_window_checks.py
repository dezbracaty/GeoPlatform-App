"""Observe real owned Windows processes; no startup delay or readiness bypass."""
import argparse
import ctypes as c
from ctypes import wintypes as w
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import sys
import time
import uuid
import zlib

parser = argparse.ArgumentParser()
parser.add_argument('--application', type=Path, required=True)
parser.add_argument('--journal-tools',type=Path,default=Path(__file__).resolve().parents[3] / 'GPlatform-ui-sdk-build/ThirdParty/JournalSystem/tools')
parser.add_argument('--locale',choices=['en_US','zh_CN'])
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--cases',default='normal,minimize,failure,replay')
parser.add_argument('--scale',type=float,default=1.)
args = parser.parse_args()
args.output.mkdir(parents=True,exist_ok=False)
sys.path.insert(0,str(args.journal_tools.resolve()))
from journalsystem import JournalSystem
user, gdi = c.windll.user32, c.windll.gdi32
user.SetProcessDPIAware()
for name in ('GetDC', 'GetWindowDC'):
    getattr(user, name).argtypes = [w.HWND]
    getattr(user, name).restype = w.HDC
user.ReleaseDC.argtypes = [w.HWND, w.HDC]
user.GetWindowRect.argtypes = [w.HWND, c.POINTER(w.RECT)]
user.GetWindowThreadProcessId.argtypes = [w.HWND, c.POINTER(w.DWORD)]
user.IsWindowVisible.argtypes = [w.HWND]
user.IsIconic.argtypes = [w.HWND]
user.ShowWindow.argtypes = [w.HWND, c.c_int]
user.SetForegroundWindow.argtypes = [w.HWND]
user.PrintWindow.argtypes = [w.HWND, w.HDC, w.UINT]
user.SendMessageTimeoutW.argtypes = [w.HWND,w.UINT,w.WPARAM,w.LPARAM,w.UINT,w.UINT,c.POINTER(c.c_size_t)]
user.PostMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
gdi.CreateCompatibleDC.argtypes = [w.HDC]
gdi.CreateCompatibleDC.restype = w.HDC
gdi.CreateCompatibleBitmap.argtypes = [w.HDC, c.c_int, c.c_int]
gdi.CreateCompatibleBitmap.restype = w.HBITMAP
gdi.SelectObject.argtypes = [w.HDC, w.HANDLE]
gdi.SelectObject.restype = w.HANDLE
gdi.BitBlt.argtypes = [w.HDC,c.c_int,c.c_int,c.c_int,c.c_int,w.HDC,c.c_int,c.c_int,w.DWORD]
gdi.DeleteObject.argtypes = [w.HANDLE]
gdi.DeleteDC.argtypes = [w.HDC]
gdi.GetDIBits.argtypes = [w.HDC,w.HBITMAP,w.UINT,w.UINT,c.c_void_p,c.c_void_p,w.UINT]
callback_type = c.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)

def windows(pid):
    found = []
    @callback_type
    def visit(hwnd, _):
        owner = w.DWORD()
        user.GetWindowThreadProcessId(hwnd, c.byref(owner))
        if owner.value == pid and user.IsWindowVisible(hwnd):
            rect = w.RECT()
            user.GetWindowRect(hwnd, c.byref(rect))
            width, height = rect.right-rect.left, rect.bottom-rect.top
            if width > 100 and height > 100:
                found.append((hwnd, width, height))
        return True
    user.EnumWindows(visit, 0)
    return sorted(found, key=lambda item: item[1]*item[2])

def capture(window, filename):
    hwnd, width, height = window
    rect = w.RECT()
    user.GetWindowRect(hwnd, c.byref(rect))
    screen = user.GetDC(None)
    dc = gdi.CreateCompatibleDC(screen)
    bitmap = gdi.CreateCompatibleBitmap(screen, width, height)
    old = gdi.SelectObject(dc, bitmap)
    try:
        if not user.PrintWindow(hwnd,dc,2):
            assert gdi.BitBlt(dc,0,0,width,height,screen,rect.left,rect.top,0x00CC0020 | 0x40000000)
        gdi.SelectObject(dc, old)
        header = struct.pack('<IiiHHIIiiII',40,width,-height,1,32,0,width*height*4,0,0,0,0)
        info = c.create_string_buffer(header)
        pixels = c.create_string_buffer(width*height*4)
        assert gdi.GetDIBits(dc,bitmap,0,height,pixels,info,0)
        raw = pixels.raw
        rows = []
        for y in range(height):
            bgra = raw[y*width*4:(y+1)*width*4]
            rgb = bytearray(width*3)
            rgb[0::3],rgb[1::3],rgb[2::3]=bgra[2::4],bgra[1::4],bgra[0::4]
            rows.append(b'\0'+rgb)
        def chunk(kind, data):
            return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data)&0xffffffff)
        png = b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',width,height,8,2,0,0,0))
        png += chunk(b'IDAT',zlib.compress(b''.join(rows)))+chunk(b'IEND',b'')
        (args.output/filename).write_bytes(png)
    finally:
        gdi.DeleteObject(bitmap)
        gdi.DeleteDC(dc)
        user.ReleaseDC(None,screen)

report = {'application':str(args.application.resolve()),'checks':[]}
def record(name, **details):
    report['checks'].append({'name':name, **details})
    (args.output/'report.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    print(name+': '+str(details),flush=True)

def run(name, arguments, minimize=False, failure=False):
    stream = (args.output/(name+'.log')).open('wb')
    env = os.environ.copy()
    env.update(GP_LOG_CONSOLE='1', GPLATFORM_JOURNAL_SESSION='1', QT_SCALE_FACTOR=str(args.scale))
    process = subprocess.Popen([str(args.application.resolve()),*arguments],stdout=stream,stderr=subprocess.STDOUT,env=env)
    captured = False
    minimized = False
    launched = time.monotonic()
    try:
        deadline = time.monotonic()+120
        while time.monotonic()<deadline:
            text = (args.output/(name+'.log')).read_text(encoding='utf-8',errors='replace')
            owned = windows(process.pid)
            if not captured and owned and 'splashPresented' in text and owned[0][1] < 1000 and time.monotonic()-launched>1.5:
                reply = c.c_size_t()
                begin=time.monotonic()
                assert user.SendMessageTimeoutW(owned[0][0],0,0,0,2,2000,c.byref(reply)), 'Startup GUI stopped responding'
                record(name+'-startup-response',status='passed_wm_null',latencyMs=round((time.monotonic()-begin)*1000))
                capture(owned[0],name+'-startup.png')
                time.sleep(.35)
                capture(owned[0],name+'-animation.png')
                captured = True
            if minimize and not minimized and len(owned)>=2 and 'stage=FirstFrame ' in text and 'stage=Ready ' not in text:
                main=owned[-1][0]
                user.ShowWindow(main,6)
                time.sleep(1)
                user.ShowWindow(main,9)
                user.SetForegroundWindow(main)
                minimized = True
            if failure and '[STARTUP] failed' in text:
                time.sleep(.4)
                owned=windows(process.pid)
                assert owned
                capture(owned[0],name+'-failed.png')
                record(name,status='passed_failure_visible',pid=process.pid,screenshot=captured)
                # Exit is a normal native close event handled by the splash.
                user.PostMessageW(owned[0][0],0x10,0,0)
                assert process.wait(timeout=20)==0
                record(name+'-exit',status='passed_clean_exit')
                return
            if '[STARTUP] stage=Ready ' in text:
                time.sleep(.5)
                text = (args.output/(name+'.log')).read_text(encoding='utf-8',errors='replace')
                owned = windows(process.pid)
                if args.locale:
                    expected='正在加载打印机和耗材预设…' if args.locale=='zh_CN' else 'Loading printer and filament presets…'
                    assert 'text='+expected in text, 'Startup locale was not applied before presets'
                assert len(owned)==1, ('splash still visible',owned)
                assert not minimize or minimized, 'Minimize/restore was not exercised'
                capture(owned[0],name+'-ready.png')
                assert text.index('splashPresented')<text.index('stage=Scene ')<text.index('stage=FirstFrame ')<text.index('stage=Ready ')<text.index('Journal service ready')
                record(name,status='passed_ready_order_and_splash_hidden',pid=process.pid,minimizeRestoreObserved=minimized,screenshot=captured)
                user.PostMessageW(owned[0][0],0x10,0,0)
                time.sleep(.4)
                client=JournalSystem(request_timeout=10)
                client.connect('127.0.0.1',int(arguments[arguments.index('--ghost-port')+1]))
                try:
                    client.send_mouse('press',button=1,buttons=1,window={'objectName':'gplatform.main'},target='window.quit.confirm')
                    try: client.send_mouse('release',button=1,buttons=0,window={'objectName':'gplatform.main'},target='window.quit.confirm')
                    except (OSError,RuntimeError): pass
                finally: client.disconnect()
                assert process.wait(timeout=20)==0
                return
            if process.poll() is not None:
                raise RuntimeError(f'{name} exited before expected state: {process.returncode}; see {name}.log')
            time.sleep(.01)
        raise TimeoutError(name+' did not reach expected state')
    finally:
        if process.poll() is None:
            subprocess.run(['taskkill','/PID',str(process.pid),'/T','/F'],stdout=subprocess.DEVNULL)
            process.wait(timeout=10)
        stream.close()

def port():
    with socket.socket() as sock:
        sock.bind(('127.0.0.1',0))
        return str(sock.getsockname()[1])

settings = Path(os.environ['LOCALAPPDATA']) / 'GPlatform/GPlatform' / (args.application.stem+'.ini')
backup = settings.read_bytes() if settings.exists() else None
try:
    if args.locale:
        current=settings.read_text(encoding='utf-8-sig') if settings.exists() else '[General]\n'
        import re
        if re.search(r'^locale=.*$',current,re.M): current=re.sub(r'^locale=.*$','locale='+args.locale,current,flags=re.M)
        else: current=current.replace('[General]','[General]\nlocale='+args.locale,1)
        settings.parent.mkdir(parents=True,exist_ok=True)
        settings.write_text(current,encoding='utf-8')
    selected=args.cases.split(',')
    if 'normal' in selected:
        run('normal',['--ghost-port',port()])
    if 'minimize' in selected:
        settings.write_text('[General]\nrememberLastPage=true\nlastPage=/settings\n\n[printGuide]\nhandled=true\n',encoding='utf-8')
        run('remember-settings-minimize',['--ghost-port',port()],minimize=True)
    if 'failure' in selected:
        run('invalid-port',['--ghost-port','0'],failure=True)
    if 'replay' in selected:
        logfile=args.output/'replay-failure.log'
        with logfile.open('wb') as stream:
            env=os.environ.copy()
            env.update(GP_LOG_CONSOLE='1',GPLATFORM_JOURNAL_SESSION='1')
            result=subprocess.run([str(args.application.resolve()),'--ghost-port','0','--journal-replay-id',uuid.uuid4().hex],env=env,stdout=stream,stderr=subprocess.STDOUT,timeout=120)
        assert result.returncode==1
        record('replay-failure',status='passed_exit_1',exitCode=result.returncode)

finally:
    if backup is not None: settings.write_bytes(backup)
    elif settings.exists(): settings.unlink()
