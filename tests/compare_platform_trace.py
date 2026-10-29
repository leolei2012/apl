"""Compare a fixed committed APL baseline with the platform migration, without rewriting goldens."""
import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--baseline', default='0eef3bb')
parser.add_argument('--report', type=Path)
parser.add_argument('--baseline-repository', type=Path, default=ROOT)
args = parser.parse_args()
compiler = os.environ.get('CC') or shutil.which('gcc')
if not compiler and os.name == 'nt':
    candidates = sorted(Path('C:/Qt/Tools').glob('mingw*/bin/gcc.exe'))
    compiler = str(candidates[-1]) if candidates else None
if not compiler:
    raise SystemExit('Existing GCC required')
revision = subprocess.check_output(['git', 'rev-parse', '--verify', args.baseline + '^{commit}'], cwd=args.baseline_repository, text=True).strip()
archive = subprocess.check_output(['git', 'archive', '--format=tar', revision, 'apl_lib', 'tests/test_output_trace.c'], cwd=args.baseline_repository)
results = []
base_temp = Path(tempfile.gettempdir()).resolve()
with tempfile.TemporaryDirectory(prefix='apl-platform-trace-', dir=base_temp) as directory:
    temporary = Path(directory).resolve()
    if temporary.parent != base_temp:
        raise RuntimeError('Unexpected temporary output location')
    baseline = temporary / 'baseline'
    baseline.mkdir()
    with tarfile.open(fileobj=io.BytesIO(archive), mode='r:') as source:
        source.extractall(baseline, filter='data')
    harness = baseline / 'tests/test_output_trace.c'
    for label, purify, humidify in [('purify', 1, 0), ('humidify', 0, 1), ('combined', 1, 1)]:
        variants = []
        for name, root in [('baseline', baseline), ('working', ROOT)]:
            exe = temporary / (label + '-' + name + ('.exe' if os.name == 'nt' else ''))
            flags = ['-std=c11', '-Wall', '-Wextra', '-Werror', '-Os', '-finput-charset=UTF-8',
                     '-I' + str(root / 'apl_lib/include'), '-I' + str(root / 'apl_lib/config'),
                     '-DAPL_COMPILE_PURIFY=' + str(purify), '-DAPL_DEFAULT_PURIFY=' + str(purify),
                     '-DAPL_COMPILE_HUMIDIFY=' + str(humidify), '-DAPL_DEFAULT_HUMIDIFY=' + str(humidify),
                     '-DAPL_CORE_DRY_S=3', '-DAPL_CORE_WATER_SHORTAGE_RUN_S=4', '-DAPL_CORE_WATER_BLINK_S=2']
            if os.name == 'nt':
                flags += ['-static-libgcc']
            subprocess.run([compiler, *flags, str(harness), str(root / 'apl_lib/src/apl_core.c'), '-o', str(exe)], check=True, timeout=60)
            output = subprocess.check_output([str(exe)], timeout=30)
            variants.append(output)
        if variants[0] != variants[1]:
            raise RuntimeError('Platform migration changed callback/state trace: ' + label)
        result = {'capabilities': label, 'records': len(variants[1].splitlines()),
                  'sha256': hashlib.sha256(variants[1]).hexdigest(), 'status': 'byte-identical'}
        results.append(result)
        print('PASS:', label, result['records'], 'callback/state records identical')
receipt = {'scope': 'same immutable baseline harness on both cores; all callback snapshots and public state fields',
           'baselineCommit': revision, 'results': results}
if args.report:
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
