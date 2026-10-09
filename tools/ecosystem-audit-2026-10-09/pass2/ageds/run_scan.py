#!/usr/bin/env python3
"""Host compile of complete real Android scan engine and parser consumers."""
import argparse,hashlib,json,pathlib,subprocess,tempfile,sys

def main():
 p=argparse.ArgumentParser();p.add_argument('--checkout',required=True);p.add_argument('--sha',required=True);p.add_argument('--compiler-dir',required=True);p.add_argument('--output',required=True);a=p.parse_args();r=pathlib.Path(a.checkout).resolve();deps=pathlib.Path(a.compiler_dir).resolve();assert subprocess.check_output(['git','-C',str(r),'rev-parse','HEAD'],text=True).strip()==a.sha
 paths=['core/src/commonMain/kotlin/dev/klbt/ageds/core/'+x for x in ['SourceScanModels.kt','SourceTextParser.kt','WavHeaderObservation.kt','WavHeaderProbe.kt']]+['androidApp/src/main/java/dev/klbt/ageds/'+x for x in ['SourceScanEngine.kt','SourceDelimitedParser.kt','SourceWorkbookParser.kt','SourceXlsParser.kt','SourceWavReader.kt']]
 with tempfile.TemporaryDirectory(prefix='ageds-scan-host-') as d:
  t=pathlib.Path(d);(t/'Serializable.kt').write_text('package kotlinx.serialization\nannotation class Serializable\n')
  (t/'ScanHost.kt').write_text('''package dev.klbt.ageds
import dev.klbt.ageds.core.*
import java.io.ByteArrayInputStream
class Provider(val content:ByteArray):SourceScanProvider{
 override val rootId="root";override val rootUri="synthetic:root";var opened=0
 override fun uri(id:String)="synthetic:$id"
 override fun children(id:String)=object:SourceScanCursor{var next=0;override fun next():SourceScanDocument?=if(next++<2)SourceScanDocument("file$next","$next.csv","text/csv",content.size.toLong())else null;override fun close(){}}
 override fun openRead(id:String)=ByteArrayInputStream(content).also{opened++}
}
fun main(){
 val content=("x\\n".repeat(10002)).toByteArray();val profile=SourceScanLimits(maxFiles=2,maxRowsPerFile=20000,maxCellsPerFile=100000)
 val direct=SourceDelimitedParser.parse(content,"synthetic:csv",profile)
 val one=SourceScanEngine(Provider(content)).scan(profile.copy(maxFiles=1),"synthetic-time")
 val two=SourceScanEngine(Provider(content)).scan(profile,"synthetic-time")
 println("direct_rows=${direct.rows.size};engine_rows=${two.files.sumOf{it.rows.size}};advertised_rows=${two.limits.maxRowsPerFile};files_profiles=${one.files.size},${two.files.size};coverage=${two.coverage}")
 println("issue_codes="+(two.issues+two.files.flatMap{it.issues}).map{it.code}.distinct().joinToString(","))
 val cancel=Provider(content);var cancelled=false;try{SourceScanEngine(cancel).scan(profile,"synthetic",{throw java.util.concurrent.CancellationException("synthetic")})}catch(e:java.util.concurrent.CancellationException){cancelled=true}
 println("cancellation_propagated=$cancelled;cancelled_reads=${cancel.opened}")
}
''')
  cp=':'.join(str(x) for x in deps.glob('*.jar'));stdlib=str(deps/'kotlin-stdlib-2.1.20.jar')
  c=subprocess.run(['java','-cp',cp,'org.jetbrains.kotlin.cli.jvm.K2JVMCompiler','-no-stdlib','-no-reflect','-classpath',stdlib,*[str(r/x) for x in paths],str(t/'Serializable.kt'),str(t/'ScanHost.kt'),'-d',str(t/'scan.jar')],text=True,capture_output=True)
  out={'sha':a.sha,'compile_exit':c.returncode,'compile_log':c.stderr,'source_files':[{'file':x,'sha256':hashlib.sha256((r/x).read_bytes()).hexdigest()} for x in paths],'scope':'Full production scan engine and all parser sources compiled unchanged; synthetic read-only provider, annotation stub; Kotlin2.1.20 JVM, not pinned Android build.'}
  if c.returncode:out['status']='BLOCKED';pathlib.Path(a.output).write_text(json.dumps(out,indent=2)+'\n');return 2
  ex=subprocess.run(['java','-cp',str(t/'scan.jar')+':'+stdlib,'dev.klbt.ageds.ScanHostKt'],capture_output=True,text=True);out.update(run_exit=ex.returncode,stdout=ex.stdout,stderr=ex.stderr)
  v={k:v for line in ex.stdout.splitlines() for f in line.split(';') for k,s,v in [f.partition('=')] if s}
  out['tests']=[{'id':'EA-AGEDS-003.engine_limit_profiles','acceptance':'PASS' if v.get('files_profiles')=='1,2' else 'FAIL','criterion':'maxFiles1/2 reaches real engine and changes retained file count.'},{'id':'A2-AG-005.hidden_global_budget','reproduction':'PASS' if v.get('direct_rows')=='10002' and v.get('engine_rows')=='10000' else 'FAIL','acceptance':'BLOCKED_MISSING_CONTRACT','criterion':'Effective global retained-row budget must be data/configuration with provenance and selected strategy; per-file limit may differ only with explicit effective global budget.'},{'id':'AG-CROSS.scan_cancellation','acceptance':'PASS' if v.get('cancellation_propagated')=='true' and v.get('cancelled_reads')=='0' else 'FAIL','criterion':'Cancellation propagates without publication/read, rather than becoming successful partial scan.'}]
  pathlib.Path(a.output).write_text(json.dumps(out,indent=2)+'\n');print(ex.stdout);return int(any(x['acceptance']!='PASS' for x in out['tests']))
if __name__=='__main__':sys.exit(main())
