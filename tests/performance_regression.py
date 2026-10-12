"""Differential core validation against immutable pre-optimization sources."""
import argparse, os, re, subprocess, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
BASE = '86195571fac4fea3301758bcc92a5a14e6960172'
p = argparse.ArgumentParser()
p.add_argument('--cc', default='D:/msys64/mingw64/bin/gcc.exe')
p.add_argument('--render-matrix', action='store_true')
a = p.parse_args()
env = dict(os.environ); env['PATH'] = str(Path(a.cc).parent)+os.pathsep+env['PATH']
with tempfile.TemporaryDirectory(prefix='gbc-differential-') as d:
    work = Path(d)
    hooks = work/'hooks.c'
    hooks.write_text('#include "gbc.h"\n'+(ROOT/'port/gbc_fs_impl.h').read_text(encoding='utf-8')
                     .replace('GBC_WEAK ', '').replace('#pragma once','')+'\n')
    render_units = [('gbc_timer',['reset','step','write_div','write_tac']),
                    ('gbc_ppu',['color','reset','step','render_scanline','update_lyc'])]
    cases = [('port','performance_regression.c',render_units, []),
                               ('sound_port','apu_performance_regression.c',
                                [('gbc_apu',['reset','read','write','step'])], [])]
    if a.render_matrix:
        for depth, swap in [(16, 1), (32, 0)]:
            cases.append(('feature_port', 'performance_regression.c', render_units,
                          ['GBC_ENABLE_SAVE_STATE=0', 'GBC_ENABLE_BOOT_ROM=1',
                           'GBC_COLOR_DEPTH='+str(depth), 'GBC_COLOR_SWAP='+str(swap)]))
    for config in ('port', 'sound_port'):
        for bound in (8, 64, 252):
            cases.append((config, 'halt_batch_test.c', [('gbc', ['step'])], ['GBC_HALT_BATCH_CYCLES='+str(bound)]))
    for config, test, units, defines in cases:
        print('Differential configuration:', config, defines, flush=True)
        flags = ['-std=c11','-O2','-I'+str(ROOT/'inc'),'-I'+str(ROOT/'tests'/config)]+['-D'+x for x in defines]
        objects=[]
        for unit, names in units:
            original = subprocess.check_output(['git','-C',str(ROOT),'show',BASE+':src/'+unit+'.c']).decode('utf-8')
            if unit == 'gbc':
                original = '#include \"gbc.h\"\n' + original[original.index('uint8_t gbc_step('):original.index('int gbc_set_model(')]
            if unit == 'gbc_apu':
                lines=original.splitlines(True); patch=(ROOT/'tests/reference_nr52_silence.patch').read_text().splitlines(True)
                output=[]; cursor=0; i=2
                while i<len(patch):
                    start=int(re.match(r'@@ -(\d+)',patch[i]).group(1))-1
                    output+=lines[cursor:start];cursor=start;i+=1
                    while i<len(patch) and not patch[i].startswith('@@'):
                        line=patch[i];i+=1
                        if line[0] in ' -': assert lines[cursor]==line[1:];cursor+=1
                        if line[0] in ' +': output.append(line[1:])
                original=''.join(output+lines[cursor:])
            ref=work/(unit+'.c');ref.write_text(original,encoding='utf-8');obj=work/(unit+'.o')
            rename=['-D'+unit+'_'+n+'=reference_'+(unit[4:]+'_' if unit != 'gbc' else 'gbc_')+n for n in names]
            subprocess.run([a.cc,*flags,*rename,'-c',str(ref),'-o',str(obj)],env=env,check=True)
            objects.append(obj)
        exe=work/(config+'.exe')
        args=[*flags,*map(str,sorted((ROOT/'src').glob('*.c'))),str(ROOT/'tests'/test),str(hooks),*map(str,objects),'-o',str(exe)]
        rsp=work/'compile.rsp';rsp.write_text('\n'.join('"'+s.replace('\\','/')+'"' for s in args))
        subprocess.run([a.cc,'@'+str(rsp)],env=env,check=True)
        subprocess.run([str(exe)],env=env,cwd=work,check=True,timeout=120)
