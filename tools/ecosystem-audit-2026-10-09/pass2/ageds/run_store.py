#!/usr/bin/env python3
"""Execute actual CorpusStore/serialization/cache on JVM with Android IO stubs."""
import argparse, hashlib, json, pathlib, subprocess,tempfile,sys

def main():
 p=argparse.ArgumentParser();p.add_argument('--checkout',required=True);p.add_argument('--sha',required=True);p.add_argument('--compiler-dir',required=True);p.add_argument('--serialization-dir',required=True);p.add_argument('--output',required=True);a=p.parse_args();repo=pathlib.Path(a.checkout).resolve();deps=pathlib.Path(a.compiler_dir).resolve();ser=pathlib.Path(a.serialization_dir).resolve()
 sha=subprocess.check_output(['git','-C',str(repo),'rev-parse','HEAD'],text=True).strip();assert sha==a.sha
 with tempfile.TemporaryDirectory(prefix='ageds-store-host-') as d:
  t=pathlib.Path(d);(t/'Uri.kt').write_text('package android.net\nclass Uri(val path:String){override fun toString()=path;companion object{fun parse(s:String)=Uri(s)}}\n')
  (t/'Context.kt').write_text('''package android.content
import java.io.File
import android.net.Uri
class Resolver {fun openInputStream(uri:Uri)=File(uri.path).inputStream()}
class Prefs {private val data=mutableMapOf<String,String>();fun edit()=this;fun putString(k:String,v:String):Prefs {data[k]=v;return this};fun apply(){};fun getString(k:String,default:String?)=data[k]?:default}
class Context(val filesDir:File){val contentResolver=Resolver();val prefs=Prefs();fun getSharedPreferences(name:String,mode:Int)=prefs;companion object{const val MODE_PRIVATE=0}}
''')
  (t/'StoreHost.kt').write_text('''package dev.klbt.ageds
import android.content.Context
import android.net.Uri
import java.io.File
fun main(args:Array<String>){
 val dir=File(args[0]);val c=Context(dir);val file=File(dir,"incoming.json");val store=CorpusStore(c)
 val future="""{"schemaVersion":999,"contacts":[{"phone":"synthetic-phone"}],"defaultPresetIds":[],"extension":{"preserve":7}}"""
 val supported=future.replace("999","1");file.writeText(supported);store.import(Uri.parse(file.path))
 file.writeText(future);var futureAccepted=false
 try{futureAccepted=store.import(Uri.parse(file.path)).schemaVersion==999}catch(e:Exception){}
 val reopened=CorpusStore(Context(dir)).loadCached();val raw=File(dir,"ageds-corpus-seed.json").readText();val expected=if(futureAccepted)future else supported
 println("future_version_accepted=$futureAccepted;reopened_version=${reopened?.schemaVersion};unknown_raw_preserved=${raw==expected}")
 file.writeText("{broken");var invalidRejected=false;try{store.import(Uri.parse(file.path))}catch(e:Exception){invalidRejected=true}
 println("malformed_rejected=$invalidRejected;cache_after_invalid_unchanged=${File(dir,"ageds-corpus-seed.json").readText()==expected}")
}
''')
  paths=['core/src/commonMain/kotlin/dev/klbt/ageds/core/CorpusModels.kt','androidApp/src/main/java/dev/klbt/ageds/CorpusStore.kt','androidApp/src/main/java/dev/klbt/ageds/BoundedMetadataCache.kt','androidApp/src/main/java/dev/klbt/ageds/SourceScanPublicationGate.kt']
  cp=':'.join([str(deps/'kotlin-stdlib-2.1.20.jar'),*[str(x) for x in ser.glob('kotlinx-*.jar')]])
  cmd=['java','-cp',':'.join(str(x) for x in deps.glob('*.jar')),'org.jetbrains.kotlin.cli.jvm.K2JVMCompiler','-no-stdlib','-no-reflect','-classpath',cp,'-Xplugin='+str(ser/'kotlin-serialization-compiler-plugin-embeddable-2.1.20.jar'),*[str(repo/x) for x in paths],str(t/'Uri.kt'),str(t/'Context.kt'),str(t/'StoreHost.kt'),'-d',str(t/'host.jar')]
  comp=subprocess.run(cmd,text=True,capture_output=True)
  out={'schema':'klbt.audit.ageds.store/1','sha':sha,'compile_exit':comp.returncode,'compile_log':comp.stderr,'environment':'Kotlin2.1.20/serialization1.8.0 host; repository pins2.4.20/1.11.0 unexecuted','source_files':[{'file':x,'sha256':hashlib.sha256((repo/x).read_bytes()).hexdigest()} for x in paths],'limits':['Android ContentResolver and Context are local-file stubs; no device grants/lifecycle','Actual production store/cache and real kotlinx.serialization codec run unmodified','Raw unknown fields preserved in cache does not imply typed runtime consumption']}
  if comp.returncode:out['status']='BLOCKED';pathlib.Path(a.output).write_text(json.dumps(out,indent=2)+'\n');return 2
  data=t/'data';data.mkdir();run=subprocess.run(['java','-cp',str(t/'host.jar')+':'+cp,'dev.klbt.ageds.StoreHostKt',str(data)],text=True,capture_output=True);out.update(run_exit=run.returncode,stdout=run.stdout,stderr=run.stderr)
  vals={k:v for line in run.stdout.splitlines() for part in line.split(';') for k,s,v in [part.partition('=')] if s}
  out['tests']=[{'id':'A2-AG-004.corpus_future_version','reproduction':'PASS' if vals.get('future_version_accepted')=='true' and vals.get('reopened_version')=='999' else 'FAIL','acceptance':'FAIL' if vals.get('future_version_accepted')=='true' else 'PASS','criterion':'Unsupported corpus schema is rejected or explicit migration/quarantine occurs; it must not enter current typed runtime as understood.'},{'id':'AG-CROSS.corpus_invalid_and_extensions','acceptance':'PASS' if all(vals.get(k)=='true' for k in ['unknown_raw_preserved','malformed_rejected','cache_after_invalid_unchanged']) else 'FAIL','criterion':'Unknown raw fields remain available after store reopen; malformed replacement does not reset or overwrite previous bytes.'}]
  pathlib.Path(a.output).write_text(json.dumps(out,indent=2)+'\n');print(run.stdout);return int(any(x['acceptance']!='PASS' for x in out['tests']))
if __name__=='__main__':sys.exit(main())
