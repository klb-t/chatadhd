import com.example.core.ai.*
import com.example.core.config.*
import com.example.core.discovery.*
import com.example.core.io.Command
import com.example.core.matrix.*
import android.content.Context
import org.json.JSONObject
import kotlinx.coroutines.*

private var failures=0
private var mode="all"
fun checkCase(id:String,lane:String,title:String,body:()->Boolean){
 if(mode!="all" && mode!=lane)return
 var error:String?=null
 val passed=try{body()}catch(e:Throwable){error=e.javaClass.simpleName;false}
 if(!passed)failures++
 println(JSONObject().put("id",id).put("lane",lane).put("title",title).put("status",if(passed)"PASS" else "FAIL").put("error_type",error?:JSONObject.NULL))
}
fun targetDrift(change:String):Boolean=runBlocking {
 SettingsStore.current=Settings();AiClient.reset();val h=RewriteHost();val a=h.editor.active
 h.run("fix");check(AiClient.capturedUser!=null)
 when(change){"focus"->h.editor.active=InputConnection("field-B");"revision"->a.text="newer-edit";"sensitive"->h.editor.isSensitive=true}
 AiClient.response.complete(Result.success("late-rewrite"));yield()
 val drift=h.editor.active.text=="late-rewrite"
 h.serviceScope.cancel();drift
}
fun main(args:Array<String>) {
 mode=args.firstOrNull()?:"all"
 System.setSecurityManager(object:SecurityManager(){override fun checkPermission(p:java.security.Permission){};override fun checkConnect(host:String,port:Int){throw SecurityException("External transport blocked by audit")}})
 checkCase("CKP-NET-GUARD","acceptance","real network sockets are blocked"){runCatching{java.net.Socket("127.0.0.1",80)}.exceptionOrNull() is SecurityException}
 val custom="""[{"id":"fix","system":"USER-SYSTEM","prompt":"USER {text}","replace":true}]"""
 checkCase("CKP-A-001.override.A","reproduction","built-in fix shadows custom fix"){AiTasks.byId(custom,"fix")?.systemPrompt!="USER-SYSTEM"}
 checkCase("CKP-A-001.override.B","acceptance","custom fix is effective consumer recipe"){AiTasks.byId(custom,"fix")?.systemPrompt=="USER-SYSTEM"}
 checkCase("CKP-A-001.corrupt.A","reproduction","malformed custom list silently becomes empty"){runCatching{AiTasks.parseCustom("[BROKEN")}.getOrNull()==emptyList<AiTask>()}
 checkCase("CKP-A-001.corrupt.B","acceptance","malformed custom data returns explicit failure"){runCatching{AiTasks.parseCustom("[BROKEN")}.isFailure}
 val disabled="""[{"id":"fixture","system":"fixture","disabled":true}]"""
 checkCase("CKP-A-001.unsupported.A","reproduction","unsupported disabled field is accepted and discarded"){val t=AiTasks.parseCustom(disabled);t.size==1 && !AiTasks.writeCustom(t).contains("disabled")}
 checkCase("CKP-A-001.unsupported.B","acceptance","unsupported task fields must not silently activate task"){runCatching{AiTasks.parseCustom(disabled).isEmpty()}.getOrElse{true}}
 ProviderCatalog.init(Context())
 checkCase("CKP-A-002.fallback.A","reproduction","broken bundled catalogue resolves code-owned legacy endpoint/model"){ProviderCatalog.byId("gemini")?.let{it.baseUrl.isNotBlank()&&it.defaultModel.isNotBlank()}==true}
 checkCase("CKP-A-002.fallback.B","acceptance","broken catalogue is not replaced by unselected coded provider"){ProviderCatalog.byId("gemini")==null}
 checkCase("CKP-A-002.profile.B","acceptance","two data profiles change actual catalogue consumer"){val one=Settings(customProvidersJson="""[{"id":"gemini","baseUrl":"https://example.invalid/one","defaultModel":"model-one"}]""");val two=one.copy(customProvidersJson=one.customProvidersJson.replace("one","two"));ProviderCatalog.byId("gemini",one)!!.baseUrl!=ProviderCatalog.byId("gemini",two)!!.baseUrl}
 val transform=TransformSpec("fixture","text","picture","fixture",Mapping.DETERMINISTIC,emptyList(),Invertibility.EXACT)
 val unknown=Choice(transform,Implementation("unknown","fixture",Site.APP,"fixture",Cost()))
 val medium=unknown.copy(implementation=unknown.implementation.copy(cost=Cost(Level.MEDIUM,Level.MEDIUM,Level.MEDIUM,Level.MEDIUM,Level.MEDIUM)))
 checkCase("CKP-A-003.unknown.A","reproduction","unknown cost equals medium cost for all dimensions"){Planner.price(unknown,Weights())==Planner.price(medium,Weights())}
 checkCase("CKP-A-003.metadata.B","acceptance","unknown cost metadata survives scoring"){Planner.price(unknown,Weights());unknown.implementation.cost.money==null}
 checkCase("CKP-A-003.weights.B","acceptance","two policies alter real planner rank"){val fast=unknown.implementation.copy(id="fast",cost=Cost(latency=Level.LOW,money=Level.HIGH));val cheap=unknown.implementation.copy(id="cheap",cost=Cost(latency=Level.HIGH,money=Level.LOW));fun best(w:Weights)=Planner.plan(listOf(Interpretation(Types.TEXT,Representations.UTF8)),Types.PICTURE,{true},Policy(weights=w),listOf(transform),listOf(fast,cheap)).best!!.steps.first().implementation.id
 best(Weights(latency=20.0,money=0.0))!=best(Weights(latency=0.0,money=20.0))}
 checkCase("CKP-A-003.consumer.A","reproduction","actual ConvertRunner executes default-selected route"){runBlocking {val h=ConvertHost();h.run(Command.parse("convert to=image")!!,Settings());h.executed.isNotEmpty()}}
 checkCase("A2-CKP-001.use.A","reproduction","unknown required transform is dropped and another transform executes"){runBlocking{val h=ConvertHost();h.run(Command.parse("convert to=image use=nonexistent_audit_transform")!!,Settings());h.executed.isNotEmpty()}}
 checkCase("A2-CKP-001.use.B","acceptance","unknown required transform must stop instead of execute unrelated route"){runBlocking{val h=ConvertHost();h.run(Command.parse("convert to=image use=nonexistent_audit_transform")!!,Settings());h.executed.isEmpty()}}
 for(change in listOf("focus","revision","sensitive")){
  checkCase("CKP-A-012.$change.A","reproduction","late rewrite is applied after $change change"){targetDrift(change)}
  checkCase("CKP-A-012.$change.B","acceptance","late rewrite must not replace changed $change target"){!targetDrift(change)}
 }
 checkCase("CKP-A-001.runtime-profile.B","acceptance","two custom task profiles alter actual runAiTask transport prompt"){runBlocking {suspend fun prompt(system:String):String?{AiClient.reset();SettingsStore.current=Settings(aiCustomTasksJson="""[{"id":"audit","system":"$system"}]""");val h=RewriteHost();h.run("audit");val captured=AiClient.capturedSystem;h.serviceScope.cancel();return captured};prompt("profile-one")!=prompt("profile-two")}}
 checkCase("A2-CKP-NET.token.B","acceptance","wrong token and disabled command gate reject before consumer"){NetLines.parse("wrong-token do paste","audit-fixture",true)==null && NetLines.parse("audit-fixture do paste","audit-fixture",false)==null}
 checkCase("A2-CKP-NET.revocation.B","acceptance","queued network action is invalidated by stop"){
  var deliveries=0
  val listener=com.example.engine.NetListener({deliveries++},{})
  val c=Class.forName("com.example.engine.NetListener\$Session")
  val ctor=c.declaredConstructors.single().apply{isAccessible=true};val session=ctor.newInstance(1234,"audit-fixture",true)
  val field=listener.javaClass.getDeclaredField("running").apply{isAccessible=true};field.set(listener,session)
  val method=listener.javaClass.getDeclaredMethod("deliver",String::class.java,c).apply{isAccessible=true}
  method.invoke(listener,"audit-fixture do paste",session)
  listener.stop();android.os.Handler.drain();deliveries==0
 }
 kotlin.system.exitProcess(if(failures==0)0 else 1)
}
