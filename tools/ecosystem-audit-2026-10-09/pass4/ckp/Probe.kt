import com.example.core.capture.*
import kotlinx.coroutines.*
import org.json.JSONObject
import java.net.*

private var failures=0
private var phase="all"
private fun checkCase(id:String, category:String, finding:String, expected:String, actual:Any?, ok:Boolean) {
 if(phase!="all" && phase!=category)return
 println(JSONObject().put("id",id).put("category",category).put("finding",finding).put("expected",expected).put("actual",actual).put("status",if(ok) "PASS" else "FAIL"))
 if(!ok)failures++
}
private fun frame(reversed:Boolean=false):CaptureFrame {
 val flags=if(reversed) linkedMapOf("enabled" to true,"visibleToUser" to true) else linkedMapOf("visibleToUser" to true,"enabled" to true)
 val numbers=if(reversed) linkedMapOf("columnIndex" to 2,"rowIndex" to 1) else linkedMapOf("rowIndex" to 1,"columnIndex" to 2)
 return CaptureFrame(0,"collecting",listOf(CapturedNode("0",null,"synthetic unchanged content",semantics=NodeSemantics(flags,numbers))))
}
private class Driver(val permute:Boolean=false):CaptureDriver {
 var calls=0;var scrolls=0
 override fun frame(elapsed:Long,phase:String):CaptureFrame=frame(permute && calls++%2==1).copy(elapsedMillis=elapsed,phase=phase)
 override fun scroll(forward:Boolean):Boolean {scrolls++;return true}
 override fun expand(attempted:MutableSet<String>)=false
}
private fun archive(options:CaptureOptions=CaptureOptions(autoScroll=true,seekStart=false,expandDetails=false,captureNestedScrolls=false))=ConversationArchive("synthetic.test",7,"2026-10-09T00:00:00Z",options)
private class Tree(val label:String,val kids:List<Tree?> = emptyList(),override val privateSubtree:Boolean=false,override val visible:Boolean=true):CaptureTreeNode {
 var closes=0
 override val childCount get()=kids.size
 override fun child(index:Int)=kids[index]
 override fun describe(path:String,parent:String?)=CapturedNode(path,parent,label,description=label,state=label,semantics=NodeSemantics(uniqueId=label,actions=listOf(NodeAction(1,label))))
 override fun close(){closes++}
}
fun main(args:Array<String>) {
 phase=args.getOrElse(0){"all"}
 @Suppress("DEPRECATION")
 System.setSecurityManager(object:SecurityManager(){
  override fun checkPermission(p:java.security.Permission){}
  override fun checkConnect(host:String,port:Int){throw SecurityException("external transport blocked")}
  override fun checkListen(port:Int){throw SecurityException("listen blocked")}
  override fun checkAccept(host:String,port:Int){throw SecurityException("accept blocked")}
 })
 val a=frame();val b=frame(true)
 checkCase("CKP4-CAP-01","contract","A4-CKP-001","permuted unordered flags/numbers maps remain structurally equal",a==b,a==b)
 checkCase("CKP4-CAP-02A","reproduction","A4-CKP-001","old fingerprint differs for equal maps",a.fingerprint()!=b.fingerprint(),a.fingerprint()!=b.fingerprint())
 checkCase("CKP4-CAP-02B","acceptance","A4-CKP-001","equal semantic maps yield equal fingerprint",a.fingerprint()==b.fingerprint(),a.fingerprint()==b.fingerprint())
 val fixed=Driver();val reordered=Driver(true);val af=archive();val ar=archive();val sf=CaptureSession(af,fixed);val sr=CaptureSession(ar,reordered)
 for(i in 0 until 6){sf.step(i*700L);sr.step(i*700L)}
 val out=JSONObject().put("fixedScrolls",fixed.scrolls).put("permutedScrolls",reordered.scrolls).put("fixedPhase",af.frames.last().phase).put("permutedPhase",ar.frames.last().phase)
 checkCase("CKP4-CAP-03A","reproduction","A4-CKP-001","baseline scrolls; equal-map permutations never settle",out,fixed.scrolls>0&&reordered.scrolls==0)
 checkCase("CKP4-CAP-03B","acceptance","A4-CKP-001","only map insertion order changes: equal number of scroll requests and frame phases",out,fixed.scrolls==reordered.scrolls&&af.frames.map{it.phase}==ar.frames.map{it.phase})
 val two=frame().copy(nodes=listOf(CapturedNode("0",null,"first"),CapturedNode("1",null,"second")))
 checkCase("CKP4-CAP-04","contract","A4-CKP-001","node list order remains meaningful",two.fingerprint()!=two.copy(nodes=two.nodes.reversed()).fingerprint(),two.fingerprint()!=two.copy(nodes=two.nodes.reversed()).fingerprint())
 val fast=archive(CaptureOptions(settleMillis=300));val slow=archive(CaptureOptions(settleMillis=1700));fast.append(a);slow.append(a)
 checkCase("CKP4-CAP-05A","reproduction","A4-CKP-002","different effective settle intervals export identical JSON",fast.json()==slow.json(),fast.json()==slow.json())
 val export=JSONObject(slow.json());val restored=export.getJSONObject("options").optLong("settleMillis",-1)
 checkCase("CKP4-CAP-05B","acceptance","A4-CKP-002","effective settleMillis=1700 survives exported recipe",restored,restored==1700L)
 val unknown=archive();unknown.append(a.copy(nodes=listOf(CapturedNode("0",null,"synthetic",semantics=NodeSemantics(flags=mapOf("futureFlag" to true),numbers=mapOf("futureMetric" to 7))))))
 val semantics=JSONObject(unknown.json()).getJSONArray("frames").getJSONObject(0).getJSONArray("nodes").getJSONObject(0).getJSONObject("semantics")
 checkCase("CKP4-CAP-06","contract","A4-CKP-M001","unknown semantic facts survive export",semantics,semantics.getJSONObject("flags").getBoolean("futureFlag")&&semantics.getJSONObject("numbers").getInt("futureMetric")==7)
 val hidden=Tree("SYNTHETIC_PRIVATE",privateSubtree=true);val root=Tree("SYNTHETIC_PRIVATE",listOf(Tree("public"),hidden))
 val safe=CaptureTreeReader.read(root,0,"collecting",true,{0L});val privacy=archive();privacy.append(safe)
 checkCase("CKP4-CAP-07","contract","A4-CKP-M001","private subtree and aggregate ancestor text omitted; acquired children closed",JSONObject().put("canaryAbsent",!privacy.json().contains("SYNTHETIC_PRIVATE")).put("privateChildClosed",hidden.closes),!privacy.json().contains("SYNTHETIC_PRIVATE")&&hidden.closes==1)
 val unavailable=Tree("SYNTHETIC_UNAVAILABLE",listOf(null));val missing=CaptureTreeReader.read(unavailable,0,"collecting",true,{0L})
 checkCase("CKP4-CAP-08","contract","A4-CKP-M001","missing child carries unavailability evidence and redacts aggregate",JSONObject().put("unavailable",missing.diagnostics["unavailable_children"]).put("textOmitted",missing.nodes[0].text.isEmpty()),missing.diagnostics["unavailable_children"]==1&&missing.nodes[0].text.isEmpty())
 val off=Tree("offscreen",visible=false);val offFrame=CaptureTreeReader.read(off,0,"collecting",false,{0L});val onFrame=CaptureTreeReader.read(off,0,"collecting",true,{0L})
 checkCase("CKP4-CAP-09","contract","A4-CKP-M001","offscreen selection changes text exposure without fabricating visibility",JSONObject().put("excluded",offFrame.nodes[0].text.isEmpty()).put("included",onFrame.nodes[0].text).put("visibleFlag",onFrame.nodes[0].semantics.flags["visibleToUser"]),offFrame.nodes[0].text.isEmpty()&&onFrame.nodes[0].text=="offscreen"&&onFrame.nodes[0].semantics.flags["visibleToUser"]==false)
 val failed=archive();var turns=0;val errorDriver=object:CaptureDriver {
  override fun frame(elapsed:Long,phase:String):CaptureFrame {if(turns++>0)error("SYNTHETIC_ERROR_PRIVATE");return a}
  override fun scroll(forward:Boolean)=true
  override fun expand(attempted:MutableSet<String>)=false
 };val errorSession=CaptureSession(failed,errorDriver);errorSession.step(0);errorSession.step(700)
 val failureJson=JSONObject(failed.json())
 checkCase("CKP4-CAP-10","contract","A4-CKP-M001","failed read preserves prior frame, generic reason, unverified completeness, no exception payload",JSONObject().put("reason",failed.reason).put("frames",failed.frames.size).put("completeness",failureJson.getString("completeness")),errorSession.state==CaptureSession.State.FINISHED&&failed.frames.size==1&&failed.reason=="target_changed_locked_service_unavailable_or_read_failed"&&failureJson.getString("completeness")=="unverified"&&!failed.json().contains("SYNTHETIC_ERROR_PRIVATE"))
 val truncated=archive(CaptureOptions(maxChars=1000));truncated.append(a.copy(nodes=listOf(CapturedNode("0",null,"x".repeat(2000)))))
 checkCase("CKP4-CAP-11","contract","A4-CKP-M001","resource-limited empty projection is explicitly truncated, not complete",JSONObject().put("truncated",truncated.truncated).put("nodes",truncated.frames[0].nodes.size),truncated.truncated&&JSONObject(truncated.json()).getString("completeness")=="unverified")
 val defensive=archive();val mutable=linkedMapOf("futureFlag" to true);defensive.append(a.copy(nodes=listOf(CapturedNode("0",null,"immutable",semantics=NodeSemantics(flags=mutable)))));val before=defensive.json();mutable["futureFlag"]=false
 checkCase("CKP4-CAP-12","contract","A4-CKP-M001","later producer map mutation does not edit stored observation",before==defensive.json(),before==defensive.json())
 runBlocking {
  for(kind in listOf("success","failure","sensitive")) {
   PasteConversion.gate=CompletableDeferred();val host=CustomKeyboardIme();host.run();val original=host.editor.active;val target=Field("B")
   if(kind=="sensitive")host.editor.isSensitive=true else host.editor.active=target
   PasteConversion.gate.complete(if(kind=="failure")Result.failure(IllegalStateException("synthetic failure")) else Result.success("synthetic result"));yield()
   val affected=if(kind=="sensitive")original.text.isNotEmpty() else target.text.isNotEmpty()
   val details=JSONObject().put("events",host.editor.events).put("changedRecipientModified",affected)
   checkCase("CKP4-PASTE-$kind-A","reproduction","CKP-A-012","late result/fallback modifies current field after target/privacy change",details,affected)
   checkCase("CKP4-PASTE-$kind-B","acceptance","CKP-A-012","late operation is rejected or explicitly reauthorized after target/privacy change",details,!affected)
   host.serviceScope.cancel()
  }
  PasteConversion.gate=CompletableDeferred();val host=CustomKeyboardIme();host.run();PasteConversion.gate.complete(Result.success("synthetic result"));yield()
  checkCase("CKP4-PASTE-STABLE","contract","CKP-A-012","unchanged target receives successful result",host.editor.active.text,host.editor.active.text=="synthetic result")
  host.serviceScope.cancel()
 }
 checkCase("CKP4-NETWORK-GUARD","contract","harness","all real transport is blocked",runCatching{Socket("127.0.0.1",9)}.exceptionOrNull() is SecurityException,runCatching{Socket("127.0.0.1",9)}.exceptionOrNull() is SecurityException)
 if(failures>0)kotlin.system.exitProcess(1)
}
