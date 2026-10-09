package audit.ageds
import dev.klbt.ageds.core.*
import dev.klbt.ageds.*
import kotlinx.serialization.json.*
private val tests= mutableListOf<JsonObject>()
private fun test(id:String, ok:Boolean, detail:String) {tests+=buildJsonObject{put("id",id);put("phase","contract");put("status",if(ok)"PASS" else "FAIL");put("observed",detail)}}
private fun rejected(f:()->Unit)=try{f();false}catch(_:IllegalArgumentException){true}
private class Backend:RangeAudioBackend {
 lateinit var ready:()->Unit; lateinit var fail:(String)->Unit
 var starts=0;var releases=0;var position=0L
 override fun prepare(url:String,ready:()->Unit,failure:(String)->Unit){this.ready=ready;this.fail=failure}
 override fun seek(positionMs:Long,ready:()->Unit){position=positionMs;ready()}
 override fun start(){starts++};override fun durationMs()=10_000L;override fun positionMs()=position
 override fun release(){releases++}
}
fun main(){
 val transcript=Transcript(11,7,text="AA",segments=listOf(TranscriptSegment(0.0,1.0,"A"),TranscriptSegment(1.0,2.0,"A")))
 val mutableIndices= mutableListOf(0);val selection=CitationSelection.segments(transcript,mutableIndices);mutableIndices[0]=1
 val selector=Json.parseToJsonElement("""{"kind":"segments","indices":[0],"text_join":"concatenate_exact","time_unit":"seconds","stored_time_unit":"milliseconds","rounding":"nearest_ms","precision":"segment"}""") as JsonObject
 val saved=Citation(9,7,11,0,1000,"A","synthetic-hash-not-a-custody-proof",selector)
 test("A4-AG-CIT-FROZEN",selection.request.segmentIndices==listOf(0),"Mutable caller list cannot change frozen request")
 val reversed=JsonObject(selector.entries.reversed().associate{it.key to it.value})
 test("A4-AG-CIT-OBJECT-ORDER",selection.requireMatchingCreated(saved.copy(selector=reversed)).id==9L,"Object key order does not change accepted occurrence")
 val numeric=JsonObject(selector+mapOf("indices" to Json.parseToJsonElement("[0.0]")))
 test("A4-AG-CIT-NUMERIC",selection.requireMatchingCreated(saved.copy(selector=numeric)).id==9L,"For numeric selector index only, exact decimal equality 0 == 0.0; not a global coercion rule")
 test("A4-AG-CIT-WRONG-OCCURRENCE",rejected{selection.requireMatchingCreated(saved.copy(selector=JsonObject(selector+mapOf("indices" to JsonArray(listOf(JsonPrimitive(1)))))))},"Same quote and timestamps cannot authorize different stored occurrence")
 test("A4-AG-CIT-WRONG-VERSION",rejected{selection.requireMatchingCreated(saved.copy(derivedTextId=12))},"Changed transcript ID rejected")
 test("A4-AG-CIT-LIST-ORDER",rejected{CitationSelection.segments(transcript,listOf(1,0))},"Message/segment order is meaningful, reversed list rejected")
 test("A4-AG-CIT-UNSUPPORTED-SELECTOR",rejected{selection.requireMatchingCreated(saved.copy(selector=JsonObject(selector+mapOf("extension" to JsonPrimitive(1)))))},"Closed create-response selector contract rejects unknown operative fields explicitly")
 val huge=transcript.copy(text="synthetic",segments=listOf(TranscriptSegment(0.0,10002.0,"synthetic",List(10001){TranscriptWord(it.toDouble(),(it+1).toDouble(),"w$it")})))
 val display=CitationDisplayProjection.words(huge)
 test("A4-AG-DISPLAY-PARTIAL",display.truncated&&display.words.size==10000&&display.words.last().first==WordRef(0,9999)&&huge.segments[0].words.size==10001,"Display is capped with explicit partial flag; original indices and 10001 canonical words retained")
 test("A4-AG-DISPLAY-EMPTY",!CitationDisplayProjection.words(null).truncated&&CitationDisplayProjection.words(null).words.isEmpty(),"Null view projection tested only; no claim unavailable source means empty graph")
 var now=0L;val backends= mutableListOf<Backend>();val controller=RangePlaybackController({now}){Backend().also{backends+=it}}
 controller.play("synthetic:A",100,200);val a=backends.last();controller.play("synthetic:B",300,400);val b=backends.last();a.ready();a.fail("old failure");b.ready()
 test("A4-AG-AUDIO-EPOCH",a.starts==0&&a.releases==1&&b.starts==1&&controller.status.startsWith("Odtwarzanie"),"Late callbacks of A do not start or stop B")
 b.position=400;controller.tick();test("A4-AG-AUDIO-END",b.releases==1&&controller.status=="Zatrzymane","Saved end releases actual controller backend")
 controller.play("synthetic:C",100,200);val c=backends.last();now+=30_000_000_001;controller.tick();c.ready()
 test("A4-AG-AUDIO-TIMEOUT",c.releases==1&&c.starts==0&&controller.status.contains("limit"),"Timeout stays an error; stale ready cannot resurrect playback")
 println(buildJsonObject{put("schema","klbt.audit.pass4.ageds-kotlin/1");put("tests",JsonArray(tests));put("boundaries","Actual production Kotlin; fake audio decoder backend and virtual clock only. No Android MediaPlayer/SAF/device, acoustic accuracy or network claim.")})
}
