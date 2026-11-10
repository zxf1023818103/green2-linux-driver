#!/usr/bin/env python3
"""Repack a supplied, licensed Android baseline with externally built artifacts.
Uses prebuilt image tools; preserves bootloader, GPT and userdata. Does not flash.
"""
from pathlib import Path
import argparse,hashlib,json,os,re,subprocess,zipfile
p=argparse.ArgumentParser()
for name in ['baseline','output','tools','key','chain-public-key','layout','hal','pasn-libs','module','apk']:p.add_argument('--'+name,required=True,type=Path)
a=p.parse_args();dest=a.output.resolve();dest.mkdir(parents=True,exist_ok=False);stage=dest.parent/'staging';stage.mkdir(exist_ok=True)
env=dict(os.environ,PATH=str(a.tools.resolve())+':'+os.environ['PATH']);avb=a.tools/'avbtool'
def run(args,**kw):return subprocess.run([str(x) for x in args],check=True,env=env,**kw)
def info(path):return subprocess.check_output([str(avb),'info_image','--image',str(path)],text=True)
def sha(path):
 with path.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
for source in a.baseline.glob('*.img'):
 target=dest/source.name
 if source.stem=='vendor':run(['cp','--reflink=auto','--sparse=always',source,target]);target.chmod(0o644)
 elif source.stem not in ['super','vbmeta']:target.symlink_to(source.resolve())
vendor=dest/'vendor.img';run([avb,'erase_footer','--image',vendor])
def exists(path):return bool(subprocess.check_output(['debugfs','-R','stat '+path,str(vendor)],stderr=subprocess.DEVNULL).strip())
def debug(cmd):
 result=run(['debugfs','-w','-R',cmd,vendor],capture_output=True,text=True)
 if any(s in result.stderr for s in ['Could not allocate','File not found','write: File exists','Command not found']):raise RuntimeError(result.stderr)
def label(path,context):
 f=stage/'context';f.write_bytes(context.encode()+b'\0');debug(f'ea_set -f {f} {path} security.selinux')
def mkdir(path,context='u:object_r:vendor_app_file:s0'):
 if not exists(path):debug('mkdir '+path)
 debug(f'set_inode_field {path} mode 040755');label(path,context)
def put(path,body,context='u:object_r:vendor_file:s0',mode=0o100644):
 f=stage/('file-'+path.strip('/').replace('/','_'));f.write_bytes(body)
 if exists(path):debug('rm '+path)
 debug(f'write {f} {path}');debug(f'set_inode_field {path} mode 0{mode:o}');debug(f'set_inode_field {path} uid 0');debug(f'set_inode_field {path} gid 0');label(path,context)
 actual=subprocess.check_output(['debugfs','-R','cat '+path,str(vendor)],stderr=subprocess.DEVNULL);assert actual==body,path
put('/bin/hw/android.hardware.wifi-service',a.hal.read_bytes(),'u:object_r:hal_wifi_default_exec:s0',0o100755)
put('/lib/board-modules/whc_usb.ko',a.module.read_bytes())
for name in ['libpasn.so','libgreen2pasn.so','libsrtdroid.so']:put('/lib64/'+name,(a.pasn_libs/name).read_bytes())
for path in ['/priv-app','/priv-app/FlashStudio','/priv-app/FlashStudio/lib','/priv-app/FlashStudio/lib/arm64']:mkdir(path)
put('/priv-app/FlashStudio/FlashStudio.apk',a.apk.read_bytes(),'u:object_r:vendor_app_file:s0')
with zipfile.ZipFile(a.apk) as archive:
 for name in archive.namelist():
  if name.startswith('lib/arm64-v8a/') and name.endswith('.so'):put('/priv-app/FlashStudio/lib/arm64/'+Path(name).name,archive.read(name),'u:object_r:vendor_app_file:s0')
mkdir('/etc/default-permissions','u:object_r:vendor_configs_file:s0')
xml='<?xml version="1.0" encoding="utf-8"?>\n<exceptions><exception package="cn.zenghome.flashstudio">'
for permission in ['NEARBY_WIFI_DEVICES','BLUETOOTH_CONNECT','BLUETOOTH_ADVERTISE','CAMERA']:xml+=f'<permission name="android.permission.{permission}" fixed="true"/>'
xml+='</exception></exceptions>\n';put('/etc/default-permissions/flashstudio.xml',xml.encode(),'u:object_r:vendor_configs_file:s0')
run(['e2fsck','-fn',vendor]);old=info(a.baseline/'vendor.img');salt=re.search(r'^\s+Salt:\s+([0-9a-f]+)',old,re.M).group(1)
run([avb,'add_hashtree_footer','--image',vendor,'--partition_name','vendor','--partition_size',(a.baseline/'vendor.img').stat().st_size,'--hash_algorithm','sha256','--salt',salt])
base_meta=info(a.baseline/'vbmeta.img');rollback=re.search(r'^Rollback Index:\s+(\d+)',base_meta,re.M).group(1)
args=[avb,'make_vbmeta_image','--output',dest/'vbmeta.img','--key',a.key,'--algorithm','SHA256_RSA4096','--rollback_index',rollback,'--padding_size','65536']
for name,location in [('boot',2),('init_boot',3),('vbmeta_vendor_dlkm',4),('vbmeta_system_dlkm',5)]:args+=['--chain_partition',f'{name}:{location}:{a.chain_public_key}']
for name in ['system','vendor','dtbo','vendor_boot']:args+=['--include_descriptors_from_image',dest/(name+'.img')]
for name,value in re.findall(r"^\s+Prop: ([^ ]+) -> '(.*)'$",base_meta,re.M):args+=['--prop',name+':'+value]
run(args);run([avb,'verify_image','--image',dest/'vbmeta.img','--key',a.key,'--follow_chain_partitions'])
layout=json.loads(a.layout.read_text());block=layout['block_devices'][0]
args=[a.tools/'lpmake','--metadata-size','65536','--metadata-slots','3','--device',f"super:{block['size']}:{block.get('alignment','1048576')}:0",'--super-name','super','--block-size','4096','--virtual-ab','--sparse','--output',dest/'super.img']
for group in layout['groups']:
 if group['name']!='default':args+=['--group',f"{group['name']}:{group['maximum_size']}"]
for part in layout['partitions']:
 size=int(part.get('size',0));args+=['--partition',f"{part['name']}:readonly:{size}:{part['group_name']}"]
 if size:args+=['--image',f"{part['name']}={dest/(part['name'].removesuffix('_a')+'.img')}"]
run(args)
report={'avb_verified':True,'super_roundtrip_verified':False,'userdata_modified':False,'gpt_changed':False,'device_written':False,'apk_preinstalled':True,'files':{}}
for path in dest.glob('*.img'):
 if not path.is_symlink():report['files'][path.name]={'sha256':sha(path),'bytes':path.stat().st_size}
(dest.parent/'image-fixes.json').write_text(json.dumps(report,indent=2)+'\n');(dest.parent/'expected-lp-layout.json').write_text(json.dumps(layout,indent=2)+'\n')
print('Android candidate signed; no device writes.')
