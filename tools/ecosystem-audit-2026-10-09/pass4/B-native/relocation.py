#!/usr/bin/env python3
"""Minimal real Catalog relocation probe. Needs driver built by run.py (or --driver)."""
import argparse,hashlib,json,pathlib,subprocess,tempfile
p=argparse.ArgumentParser();p.add_argument('--driver',required=True,type=pathlib.Path);p.add_argument('--repo',required=True,type=pathlib.Path);p.add_argument('--sha',required=True);p.add_argument('--out',required=True,type=pathlib.Path);p.add_argument('--phase',choices=['reproduction','acceptance','both'],default='both');a=p.parse_args();assert subprocess.check_output(['git','-C',str(a.repo),'rev-parse','HEAD'],text=True).strip()==a.sha
calls=[]
with tempfile.TemporaryDirectory(prefix='a4-relocation-') as td:
 t=pathlib.Path(td);data=t/'data';old=t/'old.json';new=t/'new.json';old.write_text('{"x":1}')
 def call(op,**kw):
  req={'op':op,'data_dir':str(data),**kw};r=subprocess.run([str(a.driver.resolve())],input=json.dumps(req),capture_output=True,text=True,timeout=60);response=json.loads(r.stdout);calls.append({'request':req,'response':response,'exit':r.returncode});return response
 call('scan',config={'sources':[str(old)],'retain_raw':'none'});uid=call('query')['value'][0]['unit']['id'];before=call('read',id=uid);old.rename(new)
 call('scan',config={'sources':[str(new)],'retain_raw':'none'});normal=call('read',id=uid)
 call('scan',config={'sources':[str(new)],'retain_raw':'none','force':True});forced=call('read',id=uid)
report={'sha':a.sha,'fixture':'{"x":1}','fixture_bytes':7,'calls':calls,'reproduction':{'status':'PASS' if before['ok'] and not normal['ok'] and not forced['ok'] else 'FAIL'},'acceptance':{'status':'PASS' if normal['ok'] and forced['ok'] and normal['value']==forced['value']=='{"x":1}' else 'FAIL'},'driver_sha256':hashlib.sha256(a.driver.read_bytes()).hexdigest(),'limitation':'explicit rescan relocation; not unauthorized automatic remote fallback'}
a.out.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({'reproduction':report['reproduction'],'acceptance':report['acceptance']}))

raise SystemExit(int((a.phase in ('acceptance','both') and report['acceptance']['status']!='PASS') or (a.phase in ('reproduction','both') and report['reproduction']['status']!='PASS')))
