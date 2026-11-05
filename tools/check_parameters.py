"""Check the APL-owned schema, Excel template and examples against actual C code.

Uses Python standard library and an installed native GCC. No device/serial access.
"""
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
from xml.etree import ElementTree as ET
from zipfile import ZipFile

ROOT = Path(__file__).resolve().parents[1]
DIRECTORY = ROOT / 'apl_lib/parameters'


def require(condition, message):
    if not condition:
        raise ValueError(message)


def workbook_cells(filename):
    ns = {'m': 'http://schemas.openxmlformats.org/spreadsheetml/2006/main'}
    result = {}
    with ZipFile(filename) as archive:
        shared = []
        if 'xl/sharedStrings.xml' in archive.namelist():
            shared = [''.join(t.text or '' for t in cell.findall('.//m:t', ns))
                      for cell in ET.fromstring(archive.read('xl/sharedStrings.xml'))]
        links = {r.attrib['Id']: r.attrib['Target']
                 for r in ET.fromstring(archive.read('xl/_rels/workbook.xml.rels'))}
        for sheet in ET.fromstring(archive.read('xl/workbook.xml')).find('m:sheets', ns):
            target = links[sheet.attrib['{http://schemas.openxmlformats.org/officeDocument/2006/relationships}id']]
            target = target.lstrip('/') if target.startswith('/') else 'xl/' + target
            cells = {}
            for cell in ET.fromstring(archive.read(target)).findall('.//m:c', ns):
                value = cell.find('m:v', ns)
                value = value.text if value is not None else None
                if cell.attrib.get('t') == 's':
                    value = shared[int(value)]
                elif cell.attrib.get('t') == 'inlineStr':
                    value = ''.join(t.text or '' for t in cell.findall('.//m:t', ns))
                cells[cell.attrib['r']] = value
            result[sheet.attrib['name']] = cells
    return result


def main():
    schema = json.loads((DIRECTORY / 'profile.schema.json').read_text(encoding='utf-8'))
    header = (ROOT / 'apl_lib/include/apl_core.h').read_text(encoding='utf-8')
    apl_header = (ROOT / 'apl_lib/include/apl.h').read_text(encoding='utf-8')
    version = re.search(r'APL_VERSION_STRING\s+"([^"]+)"', apl_header)[1]
    require(schema['aplVersion'] == version, 'Schema APL version differs from library')
    require(schema['schemaVersion'] == int(re.search(r'APL_PROFILE_VERSION\s+(\d+)', header)[1]),
            'Schema format version differs from APL_PROFILE_VERSION')
    struct = re.search(r'struct apl_profile\s*\{(.*?)\};', header, re.S)[1]
    members = re.findall(r'(uint(?:8|16|32)_t)\s+(\w+)\s*;', struct)
    fields = schema['parameters']
    require([(p['cType'], p['key']) for p in fields] == members, 'Schema fields/types differ from apl_profile')
    require(len({p['key'] for p in fields}) == len(fields), 'Duplicate parameter key')
    pid = schema['identity']['pid']
    require(pid['default'] == '' and pid['maxLength'] == 16 and pid['pattern'] == '^[A-Za-z0-9]*$', 'Invalid PID definition')
    require('char pid[APL_PRODUCT_PID_MAX + 1u]' in struct and '#define APL_PRODUCT_PID_MAX 16u' in header, 'C PID storage mismatch')
    mapping = schema['keyMapping']
    default_map = mapping['default']
    require(mapping['parameter'] == 'key_mapping', 'Invalid key mapping parameter name')
    require([slot['id'] for slot in mapping['slots']] == list(range(7)), 'Invalid input slot IDs')
    require([action['id'] for action in mapping['actions']] == list(range(16)), 'Invalid key action IDs')
    require(default_map == {'enabled': False, 'short': [5, 10, 7, 9, 3, 1, 12],
                            'long': [6, 11, 8, 0, 4, 2, 13]}, 'Invalid standard mapping')
    type_header = (ROOT / 'apl_lib/include/apl_type.h').read_text(encoding='utf-8')
    hmi_header = (ROOT / 'apl_lib/include/apl_hmi.h').read_text(encoding='utf-8')
    require('struct apl_keymap keymap;' in struct and '#define APL_KEYMAP_COUNT 7u' in type_header,
            'C key mapping storage mismatch')
    for p in fields:
        require(all(type(p[k]) is int for k in ['default', 'min', 'max']), 'Non-integer limits/default: ' + p['key'])
        require(p['min'] <= p['default'] <= p['max'], 'Invalid default: ' + p['key'])

    sheets = workbook_cells(DIRECTORY / 'APL参数表.xlsx')
    cells = sheets['参数配置']
    keys = [value for key, value in cells.items() if re.fullmatch(r'C\d+', key) and key not in ['C5'] and value]
    require(keys == [p['key'] for p in fields], 'Excel fields differ from schema')
    for row, p in enumerate(fields, start=6):
        require(cells[f'B{row}'] == p['name'], 'Excel label mismatch: ' + p['key'])
        require(int(cells[f'D{row}']) == p['default'] == int(cells[f'H{row}']), 'Excel default mismatch: ' + p['key'])
        require(cells[f'F{row}'] == p['cType'], 'Excel type mismatch: ' + p['key'])
        expected_range = str(p['min']) if p['min'] == p['max'] else f"{p['min']}～{p['max']}"
        require(cells[f'G{row}'] == expected_range, 'Excel range mismatch: ' + p['key'])
    info_cells = sheets['产品信息']
    metadata = {value: info_cells.get('B' + key[1:]) for key, value in info_cells.items()
                if re.fullmatch(r'A\d+', key) and int(key[1:]) >= 6}
    require(metadata['apl_version'] == schema['aplVersion'], 'Excel APL version mismatch')
    require(int(metadata['schema_version']) == schema['schemaVersion'], 'Excel schema version mismatch')
    require(metadata['repository'] == schema['source']['repository'], 'Excel repository mismatch')
    require(metadata['pid'] in ['', None], 'Excel PID default mismatch')
    require(int(metadata['schema_revision']) == schema['schemaRevision'], 'Excel schema revision mismatch')
    map_cells = sheets['按键映射']
    require(int(map_cells['B3']) == int(default_map['enabled']), 'Excel custom mapping default mismatch')
    for row, slot in enumerate(mapping['slots'], start=7):
        require(int(map_cells[f'A{row}']) == slot['id'] and map_cells[f'B{row}'] == slot['name'], 'Excel input slot mismatch')
        for column, gesture in [('C', 'short'), ('E', 'long')]:
            require(int(map_cells[f'{column}{row}']) == default_map[gesture][slot['id']], 'Excel action default mismatch')
    for row, action in enumerate(mapping['actions'], start=7):
        require(int(map_cells[f'H{row}']) == action['id'] and map_cells[f'I{row}'] == action['name']
                and map_cells[f'J{row}'] == action['description'], 'Excel action dictionary mismatch')

    examples = []
    for filename in sorted((DIRECTORY / 'examples').glob('*.json')):
        example = json.loads(filename.read_text(encoding='utf-8'))
        require(example['aplVersion'] == version and example['schemaVersion'] == schema['schemaVersion']
                and example['schemaRevision'] == schema['schemaRevision'] and example['schemaId'] == schema['schemaId'],
                'Example version mismatch: ' + filename.name)
        require(set(example['profile']) == {p['key'] for p in fields} | {'pid', 'key_mapping'}, 'Example fields mismatch: ' + filename.name)
        require(type(example['profile']['pid']) is str and re.fullmatch(r'[A-Za-z0-9]{0,16}', example['profile']['pid']) is not None, 'Invalid example PID')
        example_map = example['profile']['key_mapping']
        require(set(example_map) == {'enabled', 'short', 'long'} and type(example_map['enabled']) is bool,
                'Invalid example mapping shape: ' + filename.name)
        for gesture in ['short', 'long']:
            require(type(example_map[gesture]) is list and len(example_map[gesture]) == 7
                    and all(type(value) is int and 0 <= value < 16 for value in example_map[gesture]),
                    'Invalid example mapping action: ' + filename.name)
        for p in fields:
            value = example['profile'][p['key']]
            require(type(value) is int and p['min'] <= value <= p['max'], 'Invalid example value: ' + filename.name)
        examples.append(example)
    require(len(examples) == 3, 'Expected three product examples')

    # Compile a probe, rather than trusting a second copy of C defaults or limits.
    code = ['#include "apl.h"', '#include <assert.h>', '#include <stdio.h>', '#include <string.h>',
            'int main(void) { struct apl_profile p;', 'apl_profile_defaults(&p);',
            'assert(APL_PROFILE_OK == apl_profile_validate(&p));']
    code += ['assert(APL_KEYMAP_COUNT == 7u);', 'assert(APL_KEY_ACTION_COUNT == 16u);',
             f'assert(p.keymap.enabled == {int(default_map["enabled"])}u);']
    for action in mapping['actions']:
        code.append(f'assert(APL_KEY_ACTION_{action["key"]} == {action["id"]}u);')
    for slot in mapping['slots']:
        # Make slot-to-host enum parity compile-time evidence as well.
        symbol = f'APL_HMI_KEY_{slot["key"].upper()}'
        require(symbol in hmi_header, 'Unknown C input slot: ' + symbol)
        code.append(f'assert({symbol} == {slot["id"]}u);')
        for gesture in ['short', 'long']:
            code.append(f'assert(p.keymap.{gesture}_action[{slot["id"]}] == {default_map[gesture][slot["id"]]}u);')
    # Mapping validation is independent of runtime feature availability: known
    # actions may be retained while that feature is absent; unknown IDs reject.
    for enabled in [0, 1]:
        for action in mapping['actions']:
            code += ['apl_profile_defaults(&p);', f'p.keymap.enabled = {enabled}u;',
                     f'p.keymap.short_action[0] = {action["id"]}u;',
                     f'p.keymap.long_action[6] = {action["id"]}u;',
                     'assert(APL_PROFILE_OK == apl_profile_validate(&p));']
    code += ['apl_profile_defaults(&p);', 'p.keymap.enabled = 2u;',
             'assert(APL_PROFILE_INVALID_VALUE == apl_profile_validate(&p));']
    for gesture in ['short', 'long']:
        for slot in range(7):
            code += ['apl_profile_defaults(&p);', f'p.keymap.{gesture}_action[{slot}] = APL_KEY_ACTION_COUNT;',
                     'assert(APL_PROFILE_INVALID_VALUE == apl_profile_validate(&p));']
    for p in fields:
        code.append(f'printf("{p["key"]}=%lu\\n", (unsigned long)p.{p["key"]});')
        for value, valid in [(p['min'], True), (p['max'], True), (p['max'] + 1, False)]:
            # Avoid converting a value exceeding the storage type into a valid one.
            bits = int(re.search(r'\d+', p['cType'])[0])
            if value >= 2 ** bits:
                continue
            code += ['apl_profile_defaults(&p);', f'p.{p["key"]} = {value}UL;']
            if p['key'] == 'purify_enabled' and value == 0:
                code += ['p.sensors &= (unsigned char)~15u;']
            if p['key'] == 'humidify_enabled' and value == 0:
                code += ['p.sensors &= (unsigned char)~16u;']
            code += [f'assert(APL_PROFILE_OK {"==" if valid else "!="} apl_profile_validate(&p));']
    for example in examples:
        code += ['apl_profile_defaults(&p);']
        code += [f'p.{key} = {value}UL;' for key, value in example['profile'].items() if key not in ['pid', 'key_mapping']]
        code += [f'memcpy(p.pid, {json.dumps(example["profile"]["pid"])}, {len(example["profile"]["pid"]) + 1}u);']
        example_map = example['profile']['key_mapping']
        code += [f'p.keymap.enabled = {int(example_map["enabled"])}u;']
        for gesture in ['short', 'long']:
            code += [f'p.keymap.{gesture}_action[{slot}] = {value}u;' for slot, value in enumerate(example_map[gesture])]
        code += ['assert(APL_PROFILE_OK == apl_profile_validate(&p));']
    code += ['return 0; }']
    mingw = sorted(Path('C:/Qt/Tools').glob('mingw*/bin/gcc.exe')) if os.name == 'nt' else []
    cc = os.environ.get('CC') or shutil.which('gcc') or (str(mingw[-1]) if mingw else None)
    require(cc, 'Installed native GCC required')
    with tempfile.TemporaryDirectory(prefix='apl-parameters-') as temp:
        source = Path(temp) / 'probe.c'
        source.write_text('\n'.join(code), encoding='utf-8')
        exe = Path(temp) / ('probe.exe' if os.name == 'nt' else 'probe')
        subprocess.run([cc, '-std=c11', '-Wall', '-Wextra', '-Werror', '-O2',
                        '-I' + str(ROOT / 'apl_lib/include'), '-I' + str(ROOT / 'apl_lib/config'),
                        str(source), *map(str, sorted((ROOT / 'apl_lib/src').glob('*.c'))),
                        '-o', str(exe)], check=True, timeout=60)
        run = subprocess.run([str(exe)], check=True, capture_output=True, text=True, timeout=30)
        actual = dict(line.split('=') for line in run.stdout.splitlines())
        require({p['key']: p['default'] for p in fields} == {k: int(v) for k, v in actual.items()},
                'Schema defaults differ from actual C defaults')
    print(f'PASS: {len(fields)} fields, 7 input slots / 16 actions, Excel template, C defaults/range boundaries, and {len(examples)} product profiles')


if __name__ == '__main__':
    main()
