"""Compile and execute native platform/behavior equivalence gates using installed GCC."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--report', type=Path)
args = parser.parse_args()
mingw = sorted(Path('C:/Qt/Tools').glob('mingw*/bin/gcc.exe')) if os.name == 'nt' else []
compiler = os.environ.get('CC') or shutil.which('gcc') or (str(mingw[-1]) if mingw else None)
if not compiler:
    raise SystemExit('Existing GCC required; no dependencies installed')
sources = [ROOT / 'apl_lib/src' / name for name in ['apl.c', 'apl_core.c', 'apl_hmi.c']]
tests = ['test_apl.c', 'test_link.c', 'test_hmi_key_optional.c', 'test_sdk_release.c', 'test_apl_platform.c', 'test_filter_ratio.c']
paths = [*sources, *sorted((ROOT / 'apl_lib/include').glob('*.h')),
         *sorted((ROOT / 'apl_lib/config').glob('*.h')), *[ROOT / 'tests' / n for n in tests]]
def hashes():
    return {p.relative_to(ROOT).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
before = hashes()
results = []
with tempfile.TemporaryDirectory(prefix='apl-platform-') as directory:
    for label, purify, humidify in [('purify', 1, 0), ('humidify', 0, 1), ('combined', 1, 1)]:
        flags = ['-std=c11', '-Wall', '-Wextra', '-Werror', '-O2', '-finput-charset=UTF-8',
                 '-I' + str(ROOT / 'apl_lib/include'), '-I' + str(ROOT / 'apl_lib/config'),
                 '-DAPL_COMPILE_PURIFY=' + str(purify), '-DAPL_DEFAULT_PURIFY=' + str(purify),
                 '-DAPL_COMPILE_HUMIDIFY=' + str(humidify), '-DAPL_DEFAULT_HUMIDIFY=' + str(humidify)]
        if os.name == 'nt':
            flags += ['-static-libgcc']
        cases = [(name, [], name[:-2]) for name in tests[:-1]]
        cases += [('test_filter_ratio.c', ['-DAPL_CORE_FILTER_LIFE_H=' + str(life)], 'ratio-' + str(life))
                  for life in [1, 2, 99, 2200, 1000000]]
        cases.append(('test_apl_platform.c', ['-DAPL_CORE_FILTER_SHARED=0', '-DAPL_CORE_HAS_TURBO=0'], 'independent-no-turbo'))
        for test, extra, name in cases:
            if test == 'test_hmi_key_optional.c':
                extra += ['-DAPL_HMI_COMPILE_KEY_LIGHT=0']
            exe = Path(directory) / (label + '-' + name + ('.exe' if os.name == 'nt' else ''))
            subprocess.run([compiler, *flags, *extra, str(ROOT / 'tests' / test), *map(str, sources), '-o', str(exe)], check=True, timeout=60)
            run = subprocess.run([str(exe)], check=True, capture_output=True, text=True, timeout=30)
            results.append({'capabilities': label, 'case': name, 'status': 'passed', 'stdout': run.stdout})
            print('PASS:', label, name)
if hashes() != before:
    raise RuntimeError('APL inputs changed while checks ran')
receipt = {'scope': 'native behavior equivalence and exact percentage arithmetic; not hardware validation',
           'compiler': subprocess.check_output([compiler, '--version'], text=True).splitlines()[0],
           'inputs': before, 'results': results}
if args.report:
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(receipt, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print('PASS:', len(results), 'platform/behavior checks')
