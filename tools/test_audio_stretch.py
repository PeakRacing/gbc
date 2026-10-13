"""Compile actual integer WSOLA header; validate signal, counts and packet seams."""
import argparse,subprocess,tempfile,os,json,array,math
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--cc',type=Path,default=Path('D:/msys64/mingw64/bin/gcc.exe'));p.add_argument('--reference-header',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
root=Path(__file__).resolve().parents[1]
code=r'''#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "gbc_audio_stretch.h"
int main(int argc,char**argv){
 (void)argc;
 static struct gbc_audio_stretch s;int16_t in[512],out[640];
 FILE*i=fopen(argv[1],"rb"),*o=fopen(argv[2],"wb");assert(i&&o);
 gbc_audio_stretch_reset(&s);
 assert(gbc_audio_stretch_render(&s,out,65536)==0);
 assert(gbc_audio_stretch_feed(&s,in,GBC_STRETCH_CAP+1)==-1);
 assert(s.written==0);
 unsigned q=atoi(argv[3]);size_t n;unsigned turn=0;
 unsigned varying=!strcmp(argv[3],"varying");
 unsigned sizes[]={17,256,239,64,3};
 while((n=fread(in,4,sizes[turn++%5],i))){
  assert(!gbc_audio_stretch_feed(&s,in,n));
  for(;;){
   unsigned rates[]={65536,57016,32768,49152,65536};
   unsigned rate=varying?rates[(s.output_frames/8000)%5]:q;
   if(!gbc_audio_stretch_render(&s,out,rate))break;
   assert(fwrite(out,4,320,o)==320);
  }
 }
 assert(!fclose(i)&&!fclose(o));
 printf("input=%llu output=%llu searches=%u workspace=%u\n",(unsigned long long)s.input_frames,(unsigned long long)s.output_frames,s.searches,(unsigned)sizeof(s));
 return 0;
}'''
env=dict(os.environ);env['PATH']=str(a.cc.parent)+os.pathsep+env['PATH']
with tempfile.TemporaryDirectory() as tmp:
 t=Path(tmp);(t/'run.c').write_text(code,encoding='utf-8');exe=t/'run.exe'
 subprocess.run([str(a.cc),'-std=c11','-O2','-Wall','-Wextra','-I'+str(root/'port'),str(t/'run.c'),'-o',str(exe)],check=True,env=env)
 raw=array.array('h')
 for i in range(32000):
  raw.extend((round(10000*math.sin(2*math.pi*i/32)),round(7000*math.sin(2*math.pi*i/32))))
 (t/'in.pcm').write_bytes(raw.tobytes());result=[]
 for q in [65536,57016,49152,32768]:
  dest=t/'out.pcm';msg=subprocess.check_output([str(exe),str(t/'in.pcm'),str(dest),str(q)],env=env,text=True)
  data=array.array('h',dest.read_bytes());frames=len(data)//2
  assert abs(frames-32000*65536/q)<2300
  if q==65536:assert data.tobytes()==raw.tobytes()[:frames*4]
  crossings=sum(data[2*(i-1)]<=0<data[2*i] for i in range(1000,frames))
  freq=crossings*32000/(frames-1000);assert abs(freq-1000)<6,(q,freq)
  assert max(abs(data[2*i]) for i in range(frames))<=10000
  assert max(abs(data[2*i+1]) for i in range(frames))<=7000
  result.append({'q16':q,'frames':frames,'frequency_hz':freq,'runner':msg.strip()})
 # Low notes must retain pitch too; a narrow search can incorrectly turn
 # a 100 Hz bass into ordinary slowed/resampled audio.
 low_notes=[]
 for freq in [50,100,180,250,400,2000]:
  low=array.array('h')
  for i in range(64000): low.extend((round(10000*math.sin(2*math.pi*freq*i/32000)),round(7000*math.sin(2*math.pi*freq*i/32000))))
  (t/'in.pcm').write_bytes(low.tobytes())
  for q in [57016,49152,32768]:
   subprocess.run([str(exe),str(t/'in.pcm'),str(t/'out.pcm'),str(q)],check=True,env=env,stdout=subprocess.DEVNULL)
   d=array.array('h',(t/'out.pcm').read_bytes());n=len(d)//2
   crossings=sum(d[2*(i-1)]<=0<d[2*i] for i in range(4000,n))
   measured=crossings*32000/(n-4000)
   assert abs(measured/freq-1)<.02,(q,freq,measured)
   low_notes.append({'q16':q,'input_hz':freq,'output_hz':measured})
 # q=1 byte identity for a nonperiodic stereo fixture too.
 noise=array.array('h',( ((i*97+(i//7)*113)%65536)-32768 for i in range(64000)))
 (t/'in.pcm').write_bytes(noise.tobytes());dest=t/'out.pcm'
 subprocess.run([str(exe),str(t/'in.pcm'),str(dest),'65536'],check=True,env=env)
 assert dest.read_bytes()==noise.tobytes()[:dest.stat().st_size]
 # Repeated slow/full-speed transitions must remain bounded across ring wraps.
 # Swap independently varying stereo channels: a shared splice must preserve
 # the exact channel swap even for the full S16 range.
 extremes=array.array('h')
 for i in range(96000):extremes.extend((((i*8191+(i//7)*127)%65536)-32768,((i*53+(i//13)*8191)%65536)-32768))
 (t/'in.pcm').write_bytes(extremes.tobytes())
 transition_msg=subprocess.check_output([str(exe),str(t/'in.pcm'),str(dest),'varying'],env=env,text=True)
 transition=array.array('h',dest.read_bytes())
 assert 96000-2300<len(transition)//2<96000*2
 swapped=array.array('h')
 for i in range(96000):swapped.extend((extremes[2*i+1],extremes[2*i]))
 (t/'in.pcm').write_bytes(swapped.tobytes())
 subprocess.run([str(exe),str(t/'in.pcm'),str(t/'swap.pcm'),'varying'],check=True,env=env,stdout=subprocess.DEVNULL)
 swapped_out=array.array('h',(t/'swap.pcm').read_bytes())
 assert len(swapped_out)==len(transition)
 assert swapped_out[::2]==transition[1::2] and swapped_out[1::2]==transition[::2]
 # Optional byte comparison against the qualified original platform header.
 if a.reference_header:
  reference=t/'reference';reference.mkdir()
  (reference/'gbc_audio_stretch.h').write_text(a.reference_header.read_text().replace('handheld_gbc_stretch','gbc_audio_stretch'))
  refexe=t/'reference.exe'
  subprocess.run([str(a.cc),'-std=c11','-O2','-I'+str(reference),str(t/'run.c'),'-o',str(refexe)],check=True,env=env)
  for rate in ['65536','57016','49152','32768','varying']:
   subprocess.run([str(exe),str(t/'in.pcm'),str(t/'out.pcm'),rate],check=True,env=env,stdout=subprocess.DEVNULL)
   subprocess.run([str(refexe),str(t/'in.pcm'),str(t/'ref.pcm'),rate],check=True,env=env,stdout=subprocess.DEVNULL)
   assert (t/'out.pcm').read_bytes()==(t/'ref.pcm').read_bytes(),rate
  print('PASS original platform header PCM equivalence at all ratios/transitions')
 (a.output/'signal-tests.json').write_text(json.dumps({'tests':result,'q1_nonperiodic_stereo_bytes_equal':True,'low_notes':low_notes,'variable_rate_extreme_stereo_swap_equal':True,'variable_rate_runner':transition_msg.strip(),'actual_header':str(root/'port/gbc_audio_stretch.h')},indent=2)+'\n',encoding='utf-8')
 print('PASS actual integer stretch: counts, 1 kHz pitch, stereo amplitude, q=1 byte identity, packet seams')
