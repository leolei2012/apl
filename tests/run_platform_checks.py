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
sources = [ROOT / 'apl_lib/src' / name for name in ['apl.c', 'apl_core.c', 'apl_hmi.c', 'apl_wifi.c']]
tests = ['test_apl.c', 'test_link.c', 'test_hmi_key_optional.c', 'test_sdk_release.c', 'test_apl_platform.c', 'test_workbook_keys.c', 'test_workbook_water.c', 'test_workbook_timing.c', 'test_memory_cadence.c', 'test_workbook_panel.c', 'test_optional_modules.c', 'test_sensor_status.c', 'test_startup_preprocess.c', 'test_ambient_rgb.c', 'test_scheduler_1ms.c', 'test_snapshot_cache.c', 'test_build_trimming.c', 'test_filter_ratio.c']
paths = [ROOT / 'tests/test_key_mapping.c', ROOT / 'tests/test_profile.c', ROOT / 'tests/test_wifi_module.c', ROOT / 'tests/test_feature_trimming.c', *sources, *sorted((ROOT / 'apl_lib/include').glob('*.h')),
         *sorted((ROOT / 'apl_lib/config').glob('*.h')), *[ROOT / 'tests' / n for n in tests]]
def hashes():
    return {p.relative_to(ROOT).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
before = hashes()
results = []
with tempfile.TemporaryDirectory(prefix='apl-platform-') as directory:
    # Wi-Fi is independently linkable: no core/HMI objects or callbacks here.
    wifi_exe = Path(directory) / ('wifi-module.exe' if os.name == 'nt' else 'wifi-module')
    subprocess.run([compiler, '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-I' + str(ROOT / 'apl_lib/include'), '-I' + str(ROOT / 'apl_lib/config'),
                    str(ROOT / 'tests/test_wifi_module.c'), str(ROOT / 'apl_lib/src/apl_wifi.c'),
                    '-o', str(wifi_exe)], check=True, timeout=60)
    subprocess.run([str(wifi_exe)], check=True, timeout=30)
    results.append({'capabilities': 'wifi-only', 'case': 'standalone-module', 'status': 'passed', 'stdout': ''})
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
        cases.append(('test_workbook_panel.c', ['-DAPL_HMI_COMPILE_KEY_FILTER=0', '-DAPL_CORE_HAS_TURBO=0'], 'no-filter-key-no-turbo'))
        cases.append(('test_optional_modules.c', ['-DAPL_COMPILE_' + name + '=0' for name in ['UV','ANION','WIFI','SENSOR_PM25','SENSOR_PM10','SENSOR_HCHO','SENSOR_TVOC','SENSOR_HUMIDITY']], 'no-optional-modules'))
        cases.append(('test_optional_modules.c', ['-DAPL_COMPILE_WIFI=0'], 'no-wifi'))
        cases.append(('test_optional_modules.c', ['-DAPL_COMPILE_' + name + '=1' for name in ['UV','ANION','WIFI']], 'all-optional-modules'))
        cases.append(('test_sensor_status.c', ['-DAPL_CORE_SENSOR_FALLBACK_GEAR=3'], 'sensor-fallback-3'))
        for name, options in [
            ('trim-key-callbacks', ['APL_HMI_COMPILE_KEY_CALLBACKS=0']),
            ('trim-no-combos', ['APL_HMI_COMBO_MAX=0']),
            ('trim-no-buzzer', ['APL_HMI_COMPILE_BUZZER=0']),
            ('trim-no-cache', ['APL_CORE_COMPILE_FILTER_CACHE=0']),
            ('trim-standard-panel', ['APL_HMI_COMPILE_KEY_CALLBACKS=0', 'APL_HMI_COMBO_MAX=2', 'APL_CORE_COMPILE_FILTER_CACHE=0']),
            ('trim-panel-min', ['APL_HMI_COMPILE_KEY_CALLBACKS=0', 'APL_HMI_COMBO_MAX=0', 'APL_HMI_COMPILE_BUZZER=0', 'APL_CORE_COMPILE_FILTER_CACHE=0']),
            ('trim-core-only', ['APL_COMPILE_HMI=0', 'APL_CORE_COMPILE_FILTER_CACHE=0']),
        ]:
            cases.append(('test_build_trimming.c', ['-D' + option for option in options], name))
        optional_features = ['APL_COMPILE_TIMER','APL_COMPILE_FILTER','APL_COMPILE_FACTORY','APL_COMPILE_MEMORY','APL_COMPILE_SLEEP','APL_COMPILE_DRY','APL_HMI_COMPILE_AMBIENT']
        cases.append(('test_feature_trimming.c', [], 'feature-full'))
        for feature in optional_features:
            cases.append(('test_feature_trimming.c', ['-D' + feature + '=0'], 'without-' + feature.lower()))
        minimal_features = optional_features + ['APL_HMI_COMPILE_BUZZER','APL_HMI_COMPILE_KEY_CALLBACKS','APL_HMI_COMBO_MAX','APL_COMPILE_WIFI','APL_COMPILE_UV','APL_COMPILE_ANION','APL_CORE_HAS_TURBO','APL_COMPILE_SENSOR_PM25','APL_COMPILE_SENSOR_PM10','APL_COMPILE_SENSOR_HCHO','APL_COMPILE_SENSOR_TVOC','APL_COMPILE_SENSOR_HUMIDITY']
        cases.append(('test_feature_trimming.c', ['-D' + feature + '=0' for feature in minimal_features], 'feature-minimum'))
        cases.append(('test_feature_trimming.c', ['-D' + feature + '=0' for feature in [*minimal_features, 'APL_COMPILE_HMI']], 'feature-core-minimum'))
        if label == 'combined':
            cases.append(('test_key_mapping.c', [], 'key-mapping'))
            cases.append(('test_key_mapping.c', ['-DAPL_HMI_COMPILE_KEYMAP=0'], 'key-mapping-disabled'))
            cases.append(('test_profile.c', [], 'runtime-profiles'))
        for test, extra, name in cases:
            if test == 'test_hmi_key_optional.c':
                extra += ['-DAPL_HMI_COMPILE_KEY_LIGHT=0']
            exe = Path(directory) / (label + '-' + name + ('.exe' if os.name == 'nt' else ''))
            subprocess.run([compiler, *flags, *extra, str(ROOT / 'tests' / test), *map(str, sources), '-o', str(exe)], check=True, timeout=60)
            run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
            if run.returncode:
                raise RuntimeError(label + '/' + name + ': ' + run.stdout + run.stderr)
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
