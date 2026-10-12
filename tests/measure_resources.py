"""Compile Cortex-M4 objects only; report context size and compiler stack frames.

No link, target execution or MCU performance claim. Run from the repository root.
"""
from pathlib import Path
import subprocess
import shutil

root = Path(__file__).resolve().parents[1]
out = root / 'sdl/sdl2/build/resource_measurement'
cc = shutil.which('arm-none-eabi-gcc')
if not cc:
    raise SystemExit('arm-none-eabi-gcc is required')
nm = str(Path(cc).with_name('arm-none-eabi-nm.exe' if cc.lower().endswith('.exe') else 'arm-none-eabi-nm'))
size_tool = str(Path(cc).with_name('arm-none-eabi-size.exe' if cc.lower().endswith('.exe') else 'arm-none-eabi-size'))
variants = {
    'default': ('port', []),
    'low_memory': ('tests/low_memory_port', []),
    'low_memory_boot': ('tests/low_memory_port', ['GBC_ENABLE_BOOT_ROM=1', 'GBC_ENABLE_SAVE_STATE=1', 'GBC_ENABLE_RTC_SAVE=1']),
    'state': ('tests/feature_port', []),
    'all_sound': ('tests/feature_port', ['GBC_ENABLE_SOUND=1', 'GBC_ENABLE_BOOT_ROM=1']),
    'stream': ('tests/feature_port', ['GBC_ROM_STREAM=1']),
    'rtc_only': ('tests/feature_port', ['GBC_ENABLE_SRAM_SAVE=0', 'GBC_ENABLE_RTC_SAVE=1', 'GBC_ENABLE_SAVE_STATE=0']),
    'state_no_sram': ('tests/feature_port', ['GBC_USE_SRAM=0']),
}
for name, (conf, defines) in variants.items():
    dest = out / name
    dest.mkdir(parents=True, exist_ok=True)
    probe = dest / 'context.c'
    probe.write_text('#include "gbc.h"\nunsigned char measured_context[sizeof(gbc_t)];\n', encoding='utf-8')
    flags = ['-mcpu=cortex-m4', '-mthumb', '-std=c11', '-Os', '-fstack-usage', '-Wall', '-Wextra', '-Werror',
             '-I' + str(root / conf), '-I' + str(root / 'inc')] + ['-D' + d for d in defines]
    objects = []
    for src in [probe] + list((root / 'src').glob('*.c')):
        obj = dest / (src.stem + '.o')
        subprocess.run([cc, *flags, '-c', str(src), '-o', str(obj)], check=True)
        objects.append(obj)
    symbols = subprocess.check_output([nm, '-S', str(objects[0])], text=True)
    context = next(int(line.split()[1], 16) for line in symbols.splitlines() if 'measured_context' in line)
    rows = subprocess.check_output([size_tool, *map(str, objects[1:])], text=True).splitlines()[1:]
    text_bytes = sum(int(row.split()[0]) for row in rows)
    frames = []
    for su in dest.glob('*.su'):
        for row in su.read_text().splitlines():
            where, amount, kind = row.split('\t')
            if any(f in where for f in ('gbc_state_', 'gbc_persist_', 'gbc_boot_')):
                frames.append((where.split(':')[-1], int(amount), kind))
    print(f'{name}: context={context}, core_object_text={text_bytes}')
    for function, frame, kind in frames:
        print(f'  {function}: stack_frame={frame} ({kind})')
