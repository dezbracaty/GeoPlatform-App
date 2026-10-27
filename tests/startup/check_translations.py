"""Check English qsTr sources and the compiled Chinese startup translations."""
import argparse
import ast
import struct
from pathlib import Path
import re

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--catalog', type=Path, required=True)
args = parser.parse_args()
literal = r'''("(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*')'''
chinese = re.compile(r'[\u3400-\u4dbf\u4e00-\u9fff]')
checked = 0
for directory in ('qml', 'src'):
    for path in (root / directory).rglob('*'):
        if path.suffix not in ('.qml', '.cpp', '.h', '.hpp'):
            continue
        source = path.read_text(encoding='utf-8')
        for match in re.finditer(r'\bqsTr\(\s*' + literal, source):
            message = ast.literal_eval(match.group(1))
            assert not chinese.search(message), f'Non-English qsTr source in {path}: {message}'
            checked += 1

# The SDK's UTF-8 gettext compiler omits the metadata entry. Read the MO
# string tables directly, just as the application's Tr backend does.
raw = args.catalog.read_bytes()
magic = struct.unpack_from('<I', raw)[0]
endian = '<' if magic == 0x950412de else '>'
assert magic in (0x950412de, 0xde120495), 'Invalid MO catalog'
_, _, count, sources, targets = struct.unpack_from(endian+'5I', raw)
catalog = {}
for index in range(count):
    length, offset = struct.unpack_from(endian+'2I', raw, sources + index*8)
    size, start = struct.unpack_from(endian+'2I', raw, targets + index*8)
    catalog[raw[offset:offset+length].decode('utf-8')] = raw[start:start+size].decode('utf-8')
translated = set()
for name in ('qml/startup/StartupWindow.qml', 'src/Application/src/StartupCoordinator.cpp',
             'src/Application/src/main.cpp'):
    source = (root / name).read_text(encoding='utf-8')
    for match in re.finditer(r'\b(?:qsTr|tr)\(\s*' + literal, source):
        message = ast.literal_eval(match.group(1))
        assert not chinese.search(message), f'Non-English translation source: {message}'
        value = catalog.get(message, message)
        assert value != message and chinese.search(value), f'Missing Chinese translation: {message}'
        assert set(re.findall(r'%\d+', value)) == set(re.findall(r'%\d+', message)), message
        translated.add(message)
print(f'PASS: {checked} English qsTr call sites; {len(translated)} translated startup messages')
