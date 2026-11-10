#!/usr/bin/env python3
"""Round-trip a candidate super image with supplied prebuilt LP tools."""
from pathlib import Path
import argparse,hashlib,json,shutil,subprocess
p=argparse.ArgumentParser();p.add_argument('--work',type=Path,required=True);p.add_argument('--simg2img',type=Path,required=True);p.add_argument('--lp-tools',type=Path,required=True);a=p.parse_args();w=a.work.resolve();d=w/'images';t=w/'super-check';t.mkdir(exist_ok=False)
def run(args,**kw):return subprocess.run([str(x) for x in args],check=True,**kw)
def sha(path):
 with path.open('rb')as f:return hashlib.file_digest(f,'sha256').hexdigest()
try:
 run([a.simg2img,d/'super.img',t/'super.raw.img']);run([a.lp_tools,'lpunpack',t/'super.raw.img',t])
 actual=json.loads(subprocess.check_output([str(a.lp_tools),'lpdump','--json',str(t/'super.raw.img')],text=True));expected=json.loads((w/'expected-lp-layout.json').read_text());assert actual['groups']==expected['groups']
 assert len(actual['partitions'])==len(expected['partitions'])
 for x,y in zip(actual['partitions'],expected['partitions']):
  assert x['name']==y['name'] and int(x.get('size',0))==int(y.get('size',0)) and x['group_name']==y['group_name'] and x.get('attributes')==y.get('attributes')
 for name in ['system','vendor','vendor_dlkm','system_dlkm']:assert sha(t/(name+'_a.img'))==sha(d/(name+'.img')),name
 report=json.loads((w/'image-fixes.json').read_text());report['super_roundtrip_verified']=True;(w/'image-fixes.json').write_text(json.dumps(report,indent=2)+'\n');print('AVB/LP candidate ready: partition layout and all logical images match.')
finally:shutil.rmtree(t)
