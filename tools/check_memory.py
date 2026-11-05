"""Measure APL object reservations/private state with GCC and optional Keil C51.

This compiles an isolated source probe, not target firmware. No board is accessed.
"""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
METRICS = {
    'apl': 'apl', 'core_reserved': 'apl_core', 'core_state': 'apl_core_state',
    'hmi_reserved': 'apl_hmi', 'hmi_state': 'apl_hmi_state',
    'snapshot': 'apl_link_snapshot', 'profile': 'apl_profile',
    'key_callback': 'apl_hmi_key', 'combo_slot': 'apl_hmi_combo_slot',
    'panel': 'apl_hmi_panel',
}


def run(command):
    result = subprocess.run(command, cwd=ROOT, capture_output=True, timeout=90)
    if result.returncode:
        raise RuntimeError(result.stdout.decode(errors='replace') + result.stderr.decode(errors='replace'))
    return result.stdout.decode(errors='replace')


def tick_entry_frame(assembly):
    """Only fixed XBP reservations at entry; excludes callees and call-site spills."""
    function = re.search(r'^_\?apl_tick:\s*\n(.*?); END OF _\?apl_tick', assembly, re.MULTILINE | re.DOTALL)
    if not function or '; {' not in function[1]:
        return None
    prologue = function[1].split('; {', 1)[0]
    offsets = re.findall(r'MOV\s+DPTR,#([0-9A-F]+)H\s*\n\s*LCALL\s+\?C\?ADDXBP', prologue)
    return sum(65536 - int(value, 16) for value in offsets if int(value, 16) >= 32768)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--c51', type=Path, help='Installed Keil C51 compiler, optional')
    parser.add_argument('--report', type=Path)
    parser.add_argument('--define', action='append', default=[], metavar='APL_NAME=VALUE',
                        help='Build switch override; repeat for multiple switches')
    args = parser.parse_args()
    for value in args.define:
        if not re.fullmatch(r'APL_[A-Z0-9_]+=[0-9]+', value):
            parser.error('--define requires an APL macro and a nonnegative decimal integer')
    mingw = sorted(Path('C:/Qt/Tools').glob('mingw*/bin/gcc.exe')) if os.name == 'nt' else []
    compiler = os.environ.get('CC') or shutil.which('gcc') or (str(mingw[-1]) if mingw else None)
    if not compiler:
        raise SystemExit('Existing GCC required; no dependencies installed')
    includes = '\n'.join('#include "' + name + '.c"' for name in ['apl_wifi', 'apl_core', 'apl_hmi', 'apl'])
    expressions = ['sizeof(struct ' + name + ')' for name in METRICS.values()]
    native_print = '\n'.join('printf("' + key + '=%lu\\n", (unsigned long)' + expression + ');'
                             for key, expression in zip(METRICS, expressions))
    source = includes + '\n#ifdef __C51__\nunsigned int code apl_memory_metrics[] = {' + ','.join(expressions) + '};\n'
    source += '#else\n#include <stdio.h>\nint main(void) {\n' + native_print + '\nreturn 0; }\n#endif\n'
    results = []
    with tempfile.TemporaryDirectory(prefix='apl-memory-') as directory:
        folder = Path(directory)
        probe = folder / 'probe.c'
        probe.write_text(source, encoding='ascii')
        include_paths = [ROOT / 'apl_lib' / name for name in ['src', 'include', 'config']]
        for label, purify, humidify in [('purify', 1, 0), ('humidify', 0, 1), ('combined', 1, 1)]:
            defines = [f'APL_COMPILE_PURIFY={purify}', f'APL_DEFAULT_PURIFY={purify}',
                       f'APL_COMPILE_HUMIDIFY={humidify}', f'APL_DEFAULT_HUMIDIFY={humidify}']
            merged = dict(value.split('=', 1) for value in [*defines, *args.define])
            defines = [key + '=' + value for key, value in merged.items()]
            executable = folder / (label + ('.exe' if os.name == 'nt' else ''))
            run([compiler, '-std=c11', '-Wall', '-Wextra', '-Werror', '-O2',
                 *['-I' + str(path) for path in include_paths], *['-D' + item for item in defines],
                 str(probe), '-o', str(executable)])
            values = {key: int(value) for key, value in (line.split('=') for line in run([str(executable)]).splitlines())}
            results.append({'platform': 'native-gcc', 'capabilities': label, 'bytes': values})
            if args.c51:
                listing, assembly = folder / (label + '.lst'), folder / (label + '.a51')
                flags = ['LARGE', 'OBJECTEXTEND', 'OPTIMIZE(8,SIZE)',
                         'INCDIR(' + ','.join(map(str, include_paths)) + ')', 'DEFINE(' + ','.join(defines) + ')']
                output = run([str(args.c51.resolve()), str(probe), *flags,
                              'SRC(' + str(assembly) + ')', 'PRINT(' + str(listing) + ')'])
                if not re.search(r'0 WARNING\(S\),\s+0 ERROR\(S\)', output):
                    raise RuntimeError(output)
                full_asm = assembly.read_text(errors='replace')
                asm = full_asm.split('apl_memory_metrics:', 1)[1]
                numbers = re.findall(r'^\s*DW\s+([0-9A-F]+)H\s*$', asm, re.MULTILINE)[:len(METRICS)]
                if len(numbers) != len(METRICS):
                    raise RuntimeError('C51 size constants missing from generated assembly')
                sizes = dict(zip(METRICS, (int(number, 16) for number in numbers)))
                if sizes['apl'] > 516:
                    raise RuntimeError('C51 instance exceeds the 516-byte reservation budget')
                # Separate translation unit retains source/prologue boundaries and
                # reflects the actual root build rather than amalgamation effects.
                root_assembly = folder / (label + '-root.a51')
                root_output = run([str(args.c51.resolve()), str(ROOT / 'apl_lib/src/apl.c'), *flags,
                                   'SRC(' + str(root_assembly) + ')', 'PRINT(' + str(folder / (label + '-root.lst')) + ')'])
                if not re.search(r'0 WARNING\(S\),\s+0 ERROR\(S\)', root_output):
                    raise RuntimeError(root_output)
                code = re.search(r'CODE SIZE\s*=\s*(\d+)', listing.read_text(errors='replace'))
                results.append({'platform': 'keil-c51-large', 'capabilities': label, 'bytes': sizes,
                                'probe_code_bytes': int(code[1]) if code else None,
                                'tick_entry_frame_bytes': tick_entry_frame(root_assembly.read_text(errors='replace'))})
    receipt = {'scope': 'Compiler object sizes; unused HMI types are not allocated by a core-only root; probe CODE is not final firmware size; no worst-case stack or hardware timing claim',
               'build_defines': args.define, 'results': results}
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf8')
    print(json.dumps(receipt, indent=2))


if __name__ == '__main__':
    main()
