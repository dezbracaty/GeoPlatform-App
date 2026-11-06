#!/usr/bin/env python3
"""Check exported print artifacts against independent height-band expectations.

This does not execute an App scenario. Use the formal Journal runner/session
for UI execution, then pass its exported files and scenario metadata here.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import zipfile


MODEL_ROLES = {
    'Inner wall', 'Outer wall', 'Overhang wall', 'Sparse infill',
    'Internal solid infill', 'Internal Bridge', 'Bottom surface',
    'Top surface', 'Bridge', 'Gap infill', 'Ironing',
}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def validate_expectations(expected):
    """Reject invalid declared geometry before starting a costly UI replay."""
    supported = {'cubeDimensionsMm', 'layerCount', 'layerHeightMm', 'filament', 'physicalToolBands',
                 'maxExtrusionCrossSectionMm2', 'requireSkirt', 'requirePrimeTower', 'printableBoundsXY',
                 'printerSettingsId', 'singleExtruderMultiMaterial', 'serializedSettings', 'filamentDiameterMm'}
    require(not set(expected) - supported, f'Unsupported expectations: {sorted(set(expected) - supported)}')
    def positive(value):
        return type(value) in {int, float} and math.isfinite(value) and value > 0
    dimensions = expected.get('cubeDimensionsMm')
    require(isinstance(dimensions, list) and len(dimensions) == 3 and all(map(positive, dimensions)), 'Invalid expected dimensions')
    count, height = expected.get('layerCount'), expected.get('layerHeightMm')
    require(type(count) is int and count > 0 and positive(height), 'Invalid expected layer count/height')
    require(abs(count * height - dimensions[2]) < 1e-5, 'Expected layer count/height differs from model height')
    require(isinstance(expected.get('filament'), str) and expected['filament'], 'Missing expected filament')
    require(positive(expected.get('maxExtrusionCrossSectionMm2', .64)), 'Invalid extrusion cross-section limit')
    require(positive(expected.get('filamentDiameterMm', 1.75)), 'Invalid expected filament diameter')
    for key in ['requireSkirt', 'requirePrimeTower', 'singleExtruderMultiMaterial']:
        require(key not in expected or type(expected[key]) is bool, f'{key} must be boolean')
    bands = expected.get('physicalToolBands')
    require(isinstance(bands, list) and bands, 'Missing expected tool bands')
    previous = 0
    for band in bands:
        low, high, tool = band.get('minHeightMm'), band.get('maxHeightMm'), band.get('tool')
        require(type(low) in {int, float} and math.isfinite(low) and positive(high) and high > low,
                'Invalid expected height band')
        require(abs(low - previous) < 1e-6 and type(tool) is int and tool > 0, 'Tool bands must be contiguous with positive tool numbers')
        previous = high
    require(abs(previous - dimensions[2]) < 1e-5, 'Tool bands must cover the model height exactly')


def analyze_gcode(text, cross_section_limit=0.64, filament_diameter=1.75):
    xyz = dict(X=0., Y=0., Z=0.)
    relative_xyz, relative_e = False, False
    previous_e, flow = 0., 1.
    tool, layer_z, role, heated = None, None, '', False
    layers, roles, bounds = {}, {}, {}
    maximum = dict(areaMm2=0., line=0, command='')
    filament_area = math.pi * (filament_diameter / 2) ** 2
    for number, line in enumerate(text.splitlines(), 1):
        if line.startswith(';Z:'):
            layer_z = round(float(line[3:]), 5)
        if line.startswith(';TYPE:'):
            role = line[6:]
        code = line.split(';', 1)[0].strip()
        if not code:
            continue
        opcode = code.split()[0]
        values = {k: float(v) for k, v in re.findall(r'([XYZES])(-?\d*\.?\d+)', code)}
        if re.fullmatch(r'T\d+', opcode):
            tool, heated = int(opcode[1:]), False
        if opcode == 'M109' and values.get('S', 0) > 0:
            heated = True
        if opcode == 'M104' and values.get('S') == 0:
            heated = False
        if opcode == 'M221':
            flow = values.get('S', 100) / 100
        if opcode == 'G90':
            relative_xyz = False
        if opcode == 'G91':
            relative_xyz = True
        if opcode == 'M82':
            relative_e = False
        if opcode == 'M83':
            relative_e = True
        require(opcode != 'G20', 'Inch units are unsupported by this verifier')
        if opcode == 'G92':
            previous_e = values.get('E', previous_e)
            xyz.update({k: v for k, v in values.items() if k in xyz})
        if opcode not in {'G0', 'G1', 'G2', 'G3'}:
            continue
        start = xyz.copy()
        xyz = {k: (start[k] + values[k] if relative_xyz else values[k])
               if k in values else start[k] for k in xyz}
        delta = (values['E'] if relative_e or relative_xyz
                 else values['E'] - previous_e) if 'E' in values else 0.
        previous_e = values.get('E', previous_e)
        if delta <= 1e-7:
            continue
        require(heated, f'Line {number}: extrusion before temperature wait after T{tool}')
        require(opcode not in {'G2', 'G3'}, 'Extruding arcs need an arc-aware verifier')
        distance = math.dist(tuple(start.values()), tuple(xyz.values()))
        if distance <= 1e-8 or (xyz['X'] == start['X'] and xyz['Y'] == start['Y']):
            continue
        area = delta * flow * filament_area / distance
        require(area <= cross_section_limit + 1e-6,
                f'Line {number}: extrusion cross section {area:.6f} exceeds {cross_section_limit} mm2: {code}')
        if area > maximum['areaMm2']:
            maximum = dict(areaMm2=area, line=number, command=code)
        roles[role] = roles.get(role, 0) + 1
        box = bounds.setdefault(role, [math.inf, math.inf, -math.inf, -math.inf])
        for point in [start, xyz]:
            box[:] = [min(box[0], point['X']), min(box[1], point['Y']),
                      max(box[2], point['X']), max(box[3], point['Y'])]
        if role in MODEL_ROLES:
            require(layer_z is not None and tool is not None,
                    f'Line {number}: missing layer/tool for model extrusion')
            layers.setdefault(layer_z, set()).add(tool)
    require(layers, 'No model deposition was found')
    return dict(layers=layers, roleMoveCounts=roles, roleBoundsXY=bounds,
                maximumExtrusionCrossSection=maximum)


def verify_job(gcode, package, expected):
    validate_expectations(expected)
    raw = Path(gcode).read_bytes()
    text = raw.decode('utf8')
    result = analyze_gcode(text, expected.get('maxExtrusionCrossSectionMm2', .64), expected.get('filamentDiameterMm', 1.75))
    layers = result.pop('layers')
    count, height = expected['layerCount'], expected['layerHeightMm']
    require(len(layers) == count, f'Expected {count} model layers; found {len(layers)}')
    bands = expected['physicalToolBands']
    for layer in range(1, count + 1):
        z = round(layer * height, 5)
        matching = [band['tool'] for band in bands
                    if z > band['minHeightMm'] + 1e-6 and z <= band['maxHeightMm'] + 1e-6]
        require(len(matching) == 1, f'Expectation does not cover layer Z={z} exactly once')
        require(layers.get(z) == {matching[0]},
                f'Layer {layer}, Z={z}: expected T{matching[0]}, found {sorted(layers.get(z, []))}')
    for name, role in [('requireSkirt', 'Skirt'), ('requirePrimeTower', 'Prime tower')]:
        if expected.get(name):
            require(result['roleMoveCounts'].get(role, 0) > 0, f'Missing {role} deposition')
    if 'printableBoundsXY' in expected:
        xmin, ymin, xmax, ymax = expected['printableBoundsXY']
        for role, box in result['roleBoundsXY'].items():
            require(box[0] >= xmin and box[1] >= ymin and box[2] <= xmax and box[3] <= ymax,
                    f'{role} paths exceed configured printable bounds: {box}')
    with zipfile.ZipFile(package) as archive:
        require(archive.testzip() is None, 'Corrupt G-code 3MF archive')
        require(archive.read('Metadata/plate_1.gcode') == raw, 'Packaged and standalone G-code differ')
        require(archive.read('Metadata/plate_1.gcode.md5').decode().strip().lower()
                == hashlib.md5(raw).hexdigest(), 'Packaged G-code checksum differs')
        config = json.loads(archive.read('Metadata/project_settings.config'))
        require(config['printer_settings_id'] == expected.get('printerSettingsId', 'P3 0.4 nozzle'),
                'Printer preset differs from expectation')
        mode = str(config['single_extruder_multi_material']).lower()
        require(mode in {'0', '1', 'false', 'true'}, 'Invalid serialized single-nozzle MMU mode')
        require((mode in {'1', 'true'}) == expected.get('singleExtruderMultiMaterial', False),
                'Single-nozzle MMU mode differs from expectation')
        for key, value in expected.get('serializedSettings', {}).items():
            require(config.get(key) == value,
                    f'{key}: expected serialized value {value!r}, found {config.get(key)!r}')
        used_tools = {band['tool'] for band in bands}
        found_tools = set()
        for slot, logical in enumerate(config['filament_map']):
            tool = int(config['physical_extruder_map'][int(logical)-1])
            if tool in used_tools:
                found_tools.add(tool)
                require(config['filament_type'][slot] == expected['filament'],
                        f'T{tool} material differs from expectation')
                require(abs(float(config['filament_diameter'][slot]) - expected.get('filamentDiameterMm', 1.75)) < 1e-6,
                        f'T{tool} filament diameter differs from cross-section calculation')
        require(found_tools == used_tools, 'Expected material/tool bindings are missing')
    # A sliced-job 3MF may intentionally contain no editable mesh. Measure the
    # cube from its emitted outer-wall centerlines and advertised line width.
    width = re.search(r'^; external perimeters extrusion width = ([\d.]+)mm', text, re.M)
    require(width is not None, 'Missing outer-wall width for dimension verification')
    box = result['roleBoundsXY'].get('Outer wall')
    require(box is not None, 'Missing outer-wall deposition')
    dimensions = [box[2]-box[0]+float(width[1]), box[3]-box[1]+float(width[1]), max(layers)]
    require(all(abs(a-b) < .01 for a, b in zip(dimensions, expected['cubeDimensionsMm'])),
            f'Extruded cube dimensions differ: {dimensions}')
    result.update(verified=True, layerCount=count, packagedGcodeMatches=True,
                  scope='Exported file semantics; firmware macros and physical printing are not verified',
                  layerTools={str(z): sorted(tools) for z, tools in sorted(layers.items())},
                  cubeDimensionsFromPathsMm=dimensions,
                  files={Path(p).name: dict(bytes=Path(p).stat().st_size,
                          sha256=hashlib.sha256(Path(p).read_bytes()).hexdigest()) for p in [gcode, package]})
    return result


def validate_project_gcode_expectations(expected):
    supported = {'requiredMarkers', 'forbiddenMarkers', 'modelBoundsXYZ', 'minimumLayerCount', 'configurationValues', 'layerPolicy'}
    require(isinstance(expected, dict) and not set(expected) - supported, 'Unsupported project G-code expectation')
    bounds = expected.get('modelBoundsXYZ')
    require(isinstance(bounds, list) and len(bounds) == 6 and all(type(v) in {int, float} and math.isfinite(v) for v in bounds) and
            all(bounds[i + 3] > bounds[i] for i in range(3)), 'Invalid project model deposition bounds')
    require(type(expected.get('minimumLayerCount')) is int and expected['minimumLayerCount'] > 0, 'Invalid minimum project layer count')
    require(isinstance(expected.get('requiredMarkers'), list) and all(isinstance(v, str) and v for v in expected['requiredMarkers']), 'Missing required project G-code markers')
    require(isinstance(expected.get('forbiddenMarkers', []), list), 'Invalid forbidden G-code markers')
    require(isinstance(expected.get('configurationValues', {}), dict), 'Invalid project G-code configuration expectations')
    if 'layerPolicy' in expected:
        policy = expected['layerPolicy']
        require(isinstance(policy, dict) and set(policy) == {'initialLayerHeightMm', 'layerHeightMm'} and
                all(type(v) in {int, float} and math.isfinite(v) and v > 0 for v in policy.values()) and
                abs(bounds[2]) <= 1e-4, 'Layer policy requires positive independent heights and source base Z=0')
        first, normal = policy['initialLayerHeightMm'], policy['layerHeightMm']
        require(all(abs(v * 1000 - round(v * 1000)) <= 1e-8 for v in [first, normal]),
                'Uniform layer policy requires native three-decimal representable heights')
        # Native slicing samples each subsequent layer at its midpoint.
        count = 1 + max(0, math.ceil((bounds[5] - first) / normal - .5 - 1e-6))
        require(expected['minimumLayerCount'] == count, 'Minimum layer count differs from independent source height policy')



def project_arc_points(start, end, values, clockwise):
    """Extrema of native XY arcs with relative I/J, including full circles and helical Z."""
    require(not {'R', 'P', 'K'} & set(values) and ('I' in values or 'J' in values), 'Unsupported project arc center encoding')
    center = [start['X'] + values.get('I', 0.), start['Y'] + values.get('J', 0.)]
    radius = math.hypot(start['X'] - center[0], start['Y'] - center[1])
    end_radius = math.hypot(end['X'] - center[0], end['Y'] - center[1])
    # Native G-code rounds coordinates and I/J to three decimals. This checks
    # circular consistency of that encoding; source bounds retain their own tolerance.
    require(radius > 1e-7 and abs(radius - end_radius) <= .003, 'Invalid project arc radius')
    first = math.atan2(start['Y'] - center[1], start['X'] - center[0])
    last = math.atan2(end['Y'] - center[1], end['X'] - center[0])
    direction = -1 if clockwise else 1
    sweep = ((last - first) * direction) % math.tau
    if math.hypot(start['X'] - end['X'], start['Y'] - end['Y']) <= 1e-7:
        sweep = math.tau
    require(sweep > 1e-12, 'Empty project arc sweep')
    points = [list(start.values()), list(end.values())]
    for angle in [0., math.pi / 2, math.pi, 3 * math.pi / 2]:
        distance = ((angle - first) * direction) % math.tau
        if distance <= sweep + 1e-12:
            fraction = distance / sweep
            points.append([center[0] + radius * math.cos(angle), center[1] + radius * math.sin(angle),
                           start['Z'] + (end['Z'] - start['Z']) * fraction])
    return points


def verify_project_gcode(gcode, package, expected):
    """Verify per-plate software output; this is not a firmware/physical-print check."""
    validate_project_gcode_expectations(expected)
    raw = Path(gcode).read_bytes()
    text = raw.decode('utf8')
    for marker in expected['requiredMarkers']:
        require(marker in text, f'Missing source-plate custom G-code marker: {marker}')
    for marker in expected.get('forbiddenMarkers', []):
        require(marker not in text, f'Other plate leaked into this G-code: {marker}')
    xyz, previous_e, relative_e, relative_xyz, role = dict(X=0., Y=0., Z=0.), 0., False, False, ''
    plane, relative_center = 'G17', True
    deposited, deposition_layers = [], set()
    for line in text.splitlines():
        if line.startswith(';TYPE:'):
            role = line[6:].strip()
        elif line.startswith('; FEATURE: '):
            role = line[len('; FEATURE: '):].strip()
        code = line.split(';', 1)[0].strip()
        if not code:
            continue
        opcode = code.split()[0]
        values = {key: float(value) for key, value in re.findall(r'([XYZEIJRPKF])(-?\d*\.?\d+(?:[eE][+-]?\d+)?)', code)}
        if opcode == 'G90': relative_xyz = False
        if opcode == 'G91': relative_xyz = True
        if opcode == 'M82': relative_e = False
        if opcode == 'M83': relative_e = True
        if opcode in {'G17', 'G18', 'G19'}: plane = opcode
        if opcode == 'G90.1': relative_center = False
        if opcode == 'G91.1': relative_center = True
        require(opcode != 'G20', 'Inch G-code requires a separate verifier')
        if opcode == 'G92':
            previous_e = values.get('E', previous_e)
            xyz.update({k: v for k, v in values.items() if k in xyz})
        if opcode not in {'G0', 'G1', 'G2', 'G3'}:
            continue
        start = xyz.copy()
        xyz = {k: (start[k] + values[k] if relative_xyz else values[k]) if k in values else start[k] for k in xyz}
        delta = (values['E'] if relative_e else values['E'] - previous_e) if 'E' in values else 0.
        if 'E' in values:
            previous_e = previous_e + values['E'] if relative_e else values['E']
        if delta > 1e-7 and role in MODEL_ROLES:
            require(abs(start['Z'] - xyz['Z']) <= 1e-7, 'Project layer counting does not support nonplanar deposition')
            if opcode in {'G2', 'G3'}:
                require(plane == 'G17' and relative_center, 'Unsupported project arc plane or absolute center')
                deposited += project_arc_points(start, xyz, values, opcode == 'G2')
                deposition_layers.update([round(start['Z'], 5), round(xyz['Z'], 5)])
            elif math.dist(tuple(start.values()), tuple(xyz.values())) > 1e-7:
                deposited += [list(start.values()), list(xyz.values())]
                deposition_layers.update([round(start['Z'], 5), round(xyz['Z'], 5)])
    require(deposited, 'No actual model deposition in project G-code')
    low = [min(p[i] for p in deposited) for i in range(3)]
    high = [max(p[i] for p in deposited) for i in range(3)]
    bounds = expected['modelBoundsXYZ']
    policy = expected.get('layerPolicy')
    require(all(low[i] >= bounds[i] - .05 and high[i] <= bounds[i + 3] + .05 for i in range(2 if policy else 3)),
            f'Deposition is outside plate-local source geometry bounds: {low + high}')
    require(all(high[i] - low[i] >= .5 * (bounds[i + 3] - bounds[i]) for i in range(2 if policy else 3)), 'Printed geometry is unexpectedly small or incomplete')
    layers = deposition_layers
    require(len(layers) >= expected['minimumLayerCount'], 'Project layer generation is incomplete')
    if policy:
        first, normal = policy['initialLayerHeightMm'], policy['layerHeightMm']
        require(abs(low[2] - first) <= 1e-4 and high[2] >= max(first, bounds[5] - normal) - 1e-4 and
                high[2] <= max(first, bounds[5] + normal) + 1e-4, 'Deposition Z differs from independent layer quantization policy')
        if bounds[5] <= first + 1e-4:
            require(len(layers) == 1 and abs(high[2] - first) <= 1e-4, 'Thin source must produce exactly one initial layer')
        ordered = sorted(layers)
        require(all(abs(z - (first + index * normal)) <= 1e-4 for index, z in enumerate(ordered)),
                'Actual depositing layers differ from declared uniform layer spacing')
    with zipfile.ZipFile(package) as archive:
        members = [name for name in archive.namelist() if name.lower().endswith('.gcode')]
        require(archive.testzip() is None and len(members) == 1 and archive.read(members[0]) == raw, 'Packaged and standalone project G-code differ')
        checksum = members[0] + '.md5'
        require(checksum in archive.namelist() and archive.read(checksum).decode().strip().lower() == hashlib.md5(raw).hexdigest(),
                'Packaged project G-code checksum is missing or differs')
        config = json.loads(archive.read('Metadata/project_settings.config'))
        if policy:
            for key, target in [('initial_layer_print_height', first), ('layer_height', normal)]:
                try:
                    actual = float(config.get(key, ''))
                except (ValueError, TypeError):
                    actual = math.nan
                require(math.isfinite(actual) and abs(actual - target) <= 1e-8,
                        f'Packaged layer height differs from independent policy: {key}')
        for key, value in expected.get('configurationValues', {}).items():
            require(config.get(key) == value, f'Project G-code config differs: {key}')
    return dict(verified=True, scope='Per-plate deposition, declared source markers and exported package consistency; no physical print or complete Orca toolpath equivalence',
                layerCount=len(layers), modelDepositionBoundsXYZ=low + high,
                customGcodeStatus='verified-declared-markers' if expected['requiredMarkers'] else 'not-evaluated-no-source-markers',
                files={Path(p).name: hashlib.sha256(Path(p).read_bytes()).hexdigest() for p in [gcode, package]})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--gcode', required=True, type=Path)
    parser.add_argument('--package', required=True, type=Path)
    parser.add_argument('--metadata', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    report = verify_job(args.gcode, args.package, json.loads(args.metadata.read_text(encoding='utf8'))['expected'])
    args.output.write_text(json.dumps(report, indent=2), encoding='utf8')
    print(json.dumps(dict(verified=True, layerCount=report['layerCount'],
                         maximumExtrusionCrossSection=report['maximumExtrusionCrossSection'])))


if __name__ == '__main__':
    main()
