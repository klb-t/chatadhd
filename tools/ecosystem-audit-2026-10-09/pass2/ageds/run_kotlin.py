#!/usr/bin/env python3
"""Compile untouched source declarations/methods against small host infrastructure.
This is JVM host execution, NOT Android lifecycle/SAF/UI or pinned Gradle acceptance.
"""
import argparse, hashlib, json, pathlib, subprocess, tempfile, sys

def main():
 p=argparse.ArgumentParser();p.add_argument('--checkout',required=True);p.add_argument('--sha',required=True);p.add_argument('--compiler-dir',required=True);p.add_argument('--output',required=True);a=p.parse_args();r=pathlib.Path(a.checkout).resolve();deps=pathlib.Path(a.compiler_dir).resolve()
 sha=subprocess.check_output(['git','-C',str(r),'rev-parse','HEAD'],text=True).strip();assert sha==a.sha
 source=[]
 def segment(path,start,end):
  all=(r/path).read_text();startpos=all.index(start);endpos=all.index(end,startpos);s=all[startpos:endpos];source.append({'file':path,'line_start':all[:startpos].count('\n')+1,'line_end':all[:endpos].count('\n')+1,'sha256':hashlib.sha256(s.encode()).hexdigest()});return s
 models='core/src/commonMain/kotlin/dev/klbt/ageds/core/Models.kt';vm='androidApp/src/main/java/dev/klbt/ageds/CorpusVm.kt';main='androidApp/src/main/java/dev/klbt/ageds/MainActivity.kt'
 priority=segment(models,'data class PrioritySignals(','@Serializable\ndata class TranscriptVersion')
 install=segment(vm,'    private fun install(seed: CorpusSeed)','    fun importCorpus(')
 selection=segment(vm,'    fun selectPriority()','    private fun keywords()')
 upload=segment(main,'    suspend fun uploadAndQueue(','    suspend fun open(')
 outcomes=segment(main,'data class UploadTarget','class EvidenceVm')
 cp=':'.join(str(x) for x in deps.glob('*.jar'))
 with tempfile.TemporaryDirectory(prefix='ageds-kotlin-audit-') as d:
  t=pathlib.Path(d)
  (t/'Serialization.kt').write_text('package kotlinx.serialization\nannotation class Serializable\nannotation class SerialName(val value: String)\n')
  (t/'Json.kt').write_text('package kotlinx.serialization.json\ntypealias JsonObject = Map<String,Any?>\n')
  harness='''package dev.klbt.ageds
import dev.klbt.ageds.core.*
import java.io.File
import kotlin.coroutines.*
import java.util.concurrent.CancellationException
class State<T>(var value:T)
class SelectionHost {
 val corpus=State<CorpusSeed?>(null)
 val selectedPresetIds=mutableListOf<String>();private val manualPhones=mutableListOf<String>();private val excludedPhones=mutableListOf<String>()
 fun load(seed:CorpusSeed)=install(seed)
'''+install+selection+'''\n}
class ComponentActivity {val contentResolver=Any()}
class Uri(val text:String) {companion object {fun parse(x:String)=Uri(x)}}
object Dispatchers {val IO=Any()}
suspend fun <T> withContext(context:Any,body:suspend ()->T):T=body()
data class PendingRecording(val uri:String,val name:String,val locator:String)
data class Uploaded(val ok:Boolean=true,val artifactId:Long)
data class Queued(val jobId:Long)
class CaptureServer {val priorities=mutableListOf<Int>();val uploaded=mutableListOf<String>();var afterUpload:(()->Unit)?=null
 suspend fun uploadAudio(resolver:Any,uri:Uri,sourcePath:String):Uploaded {uploaded.add(uri.text);afterUpload?.invoke();return Uploaded(artifactId=uploaded.size.toLong())}
 suspend fun queueTranscription(id:Long,priority:Int):Queued {priorities.add(priority);return Queued(id)}
}
'''+outcomes+'''class UploadHost(val server:CaptureServer) {
 val busy=State(false);val error=State<String?>(null);val message=State<String?>(null);val progress=State<String?>(null)
 val serverUrl=State("https://audit-a.invalid");val pending=mutableListOf<PendingRecording>();val pendingSelected=mutableMapOf<String,Boolean>();val uploadOutcomes=mutableMapOf<UploadTarget,UploadOutcome>()
 fun api()=server
 fun normalizedServerUrl()=serverUrl.value.trim().trimEnd('/')
 fun outcome(recording:PendingRecording)=uploadOutcomes[UploadTarget(normalizedServerUrl(),recording.uri)]
 suspend fun fetchArtifacts(server:CaptureServer) {}
'''+upload+'''\n}
'''+priority+'''
fun run(block:suspend ()->Unit){ var result:Result<Unit>?=null;block.startCoroutine(object:Continuation<Unit>{override val context=EmptyCoroutineContext;override fun resumeWith(r:Result<Unit>){result=r}});requireNotNull(result).getOrThrow() }
fun main(){
 val seed=CorpusSeed(presets=listOf(CorpusPreset("p","P",phones=listOf("synthetic-phone"))),defaultPresetIds=listOf("p"))
 val selection=SelectionHost();selection.load(seed);selection.togglePhone("synthetic-phone");val excluded=selection.selectedPhones().isEmpty()
 selection.load(seed.copy(generatedAt="v2"));val updateRestores=selection.selectedPhones().contains("synthetic-phone")
 val restarted=SelectionHost();restarted.load(seed);val restartRestores=restarted.selectedPhones().contains("synthetic-phone")
 println("selection_excluded=$excluded;seed_update_restores=$updateRestores;host_restart_restores=$restartRestores")
 val server=CaptureServer();val up=UploadHost(server)
 for(i in 1..3){val rec=PendingRecording("synthetic:$i","clip$i","synthetic:$i");up.pending.add(rec);up.pendingSelected[rec.uri]=i!=3}
 server.afterUpload={up.serverUrl.value="https://audit-b.invalid"}
 run{up.uploadAndQueue(ComponentActivity())}
 println("priorities="+server.priorities.joinToString(",")+";uploads="+server.uploaded.joinToString(",")+";outcome_urls="+up.uploadOutcomes.keys.map{it.serverUrl}.distinct().joinToString(","))
 val cacheFile=File.createTempFile("audit-cache-", ".txt");cacheFile.delete();val cache=BoundedMetadataCache(cacheFile,32);cache.write("old")
 val gate=SourceScanPublicationGate();val old=gate.begin();gate.begin();val published=cache.writeGuarded("stale",gate,old)
 val keptAfterStale=cache.read()=="old";var oversizeRejected=false;try{cache.write("x".repeat(33))}catch(e:IllegalArgumentException){oversizeRejected=true}
 val reopened=BoundedMetadataCache(cacheFile,32).read();cacheFile.delete()
 println("stale_published=$published;old_cache_retained=$keptAfterStale;oversize_rejected=$oversizeRejected;cache_reopen=$reopened")
}
'''
  (t/'Harness.kt').write_text(harness)
  paths=[t/'Serialization.kt',t/'Json.kt',t/'Harness.kt',r/'core/src/commonMain/kotlin/dev/klbt/ageds/core/CorpusModels.kt',r/'androidApp/src/main/java/dev/klbt/ageds/BoundedMetadataCache.kt',r/'androidApp/src/main/java/dev/klbt/ageds/SourceScanPublicationGate.kt']
  for path in paths[3:]:source.append({'file':str(path.relative_to(r)),'line_start':1,'line_end':len(path.read_text().splitlines()),'sha256':hashlib.sha256(path.read_bytes()).hexdigest()})
  compile=subprocess.run(['java','-cp',cp,'org.jetbrains.kotlin.cli.jvm.K2JVMCompiler','-no-stdlib','-no-reflect','-classpath',str(deps/'kotlin-stdlib-2.1.20.jar'),*[str(x) for x in paths],'-d',str(t/'probe.jar')],capture_output=True,text=True)
  receipt={'schema':'klbt.audit.ageds.kotlin/1','sha':sha,'compiler':'Kotlin 2.1.20 / Java17 standalone, not repository pinned Gradle','source_slices':source,'compile_exit':compile.returncode,'compile_log':compile.stderr,'scope':'Unmodified declarations and method bodies compiled against fake Android UI state, captured transport, synchronous coroutine infra; real production filesystem cache and publication gate.'}
  if compile.returncode:receipt.update(status='BLOCKED');pathlib.Path(a.output).write_text(json.dumps(receipt,indent=2));return 2
  runres=subprocess.run(['java','-cp',str(t/'probe.jar')+':'+str(deps/'kotlin-stdlib-2.1.20.jar'),'dev.klbt.ageds.HarnessKt'],capture_output=True,text=True);receipt.update(run_exit=runres.returncode,stdout=runres.stdout,stderr=runres.stderr)
  vals={k:v for line in runres.stdout.splitlines() for field in line.split(';') for k,sep,v in [field.partition('=')] if sep}
  cases=[{'id':'EA-AGEDS-005.upload_priority','reproduction':'PASS' if vals.get('priorities')=='1000000,990000' else 'FAIL','acceptance':'BLOCKED_MISSING_CONTRACT','criterion':'Two configurable queue profiles must change actual uploadAndQueue priority; baseline has no profile input. No forged acceptance PASS.'},
   {'id':'EA-AGEDS-007.selection_restart','reproduction':'PASS' if vals.get('selection_excluded')=='true' and vals.get('seed_update_restores')=='true' and vals.get('host_restart_restores')=='true' else 'FAIL','acceptance':'FAIL' if vals.get('seed_update_restores')=='true' else 'PASS','criterion':'Explicit persistent exclusion survives seed update/restart. Baseline has only temporary toggle and no persistent mode; host reset is not device restart.'},
   {'id':'AG-CROSS.upload_target_capture','acceptance':'PASS' if vals.get('uploads')=='synthetic:1,synthetic:2' and vals.get('outcome_urls')=='https://audit-a.invalid' else 'FAIL','criterion':'Deselected URI is not sent; in-flight endpoint change does not redirect frozen upload batch or misattribute outcomes.'},
   {'id':'AG-CROSS.cache_fencing_restart','acceptance':'PASS' if vals.get('stale_published')=='false' and vals.get('old_cache_retained')=='true' and vals.get('oversize_rejected')=='true' and vals.get('cache_reopen')=='old' else 'FAIL','criterion':'Superseded/oversized writes preserve old bytes; actual cache reopened from disk returns old value.'}]
  receipt['tests']=cases;pathlib.Path(a.output).parent.mkdir(parents=True,exist_ok=True);pathlib.Path(a.output).write_text(json.dumps(receipt,indent=2)+'\n');print(runres.stdout);return int(any(x.get('acceptance')!='PASS' for x in cases))
if __name__=='__main__':sys.exit(main())
