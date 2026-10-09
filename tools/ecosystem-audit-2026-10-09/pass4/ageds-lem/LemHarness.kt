@file:Suppress("DEPRECATION")
package audit.lem

import android.content.Context
import androidx.room.Room
import com.example.api.*
import com.example.data.local.*
import com.example.data.model.*
import com.example.data.repository.ResearchRepository
import com.example.research.ExperimentRunner
import com.example.viewmodel.ResearchViewModel
import com.squareup.moshi.Moshi
import com.squareup.moshi.kotlin.reflect.KotlinJsonAdapterFactory
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.*
import okhttp3.*
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.ResponseBody.Companion.toResponseBody
import okio.Buffer
import retrofit2.Retrofit
import retrofit2.converter.moshi.MoshiConverterFactory
import java.io.IOException
import java.net.Socket
import java.security.Permission
import java.util.concurrent.CopyOnWriteArrayList

private val moshi = Moshi.Builder().add(KotlinJsonAdapterFactory()).build()
private val json = moshi.adapter(Any::class.java)
private val tests = mutableListOf<Map<String, Any?>>()
private val clients = mutableListOf<OkHttpClient>()
private fun check(id: String, phase: String, ok: Boolean, detail: Map<String, Any?> = emptyMap()) {
    tests.add(mapOf("id" to id, "phase" to phase, "status" to if (ok) "PASS" else "FAIL", "observed" to detail))
}
private class NoNetwork : SecurityManager() {
    override fun checkPermission(permission: Permission) {}
    override fun checkConnect(host: String?, port: Int) { throw SecurityException("AUDIT_NETWORK_BLOCKED") }
    override fun checkConnect(host: String?, port: Int, context: Any?) { throw SecurityException("AUDIT_NETWORK_BLOCKED") }
}
private class CapturedTransport : Interceptor {
    val requests = CopyOnWriteArrayList<Map<String, Any?>>()
    var mode = "embedding"
    var onRequest: (Int) -> Unit = {}
    var failureMessage = "synthetic transport failure"
    override fun intercept(chain: Interceptor.Chain): Response {
        val request = chain.request()
        val b = Buffer(); request.body?.writeTo(b)
        requests.add(mapOf("method" to request.method, "path" to request.url.encodedPath,
            "credentialLabel" to request.url.queryParameter("key"),
            "pageToken" to request.url.queryParameter("pageToken"),
            "body" to b.readUtf8().let { if (it.isEmpty()) null else json.fromJson(it) }))
        onRequest(requests.size)
        val response = when(mode) {
            "models" -> if (request.url.queryParameter("pageToken") == "audit-page-2")
                "{\"models\":[{\"name\":\"models/page-2\",\"supportedGenerationMethods\":[\"embedContent\"]}]}"
                else "{\"models\":[{\"name\":\"models/page-1\",\"supportedGenerationMethods\":[\"embedContent\"]}],\"nextPageToken\":\"audit-page-2\"}"
            else -> "{\"embedding\":{\"values\":[1.0,2.0,3.0]}}"
        }
        // Application interceptor returns without chain.proceed(): zero socket usage.
        return Response.Builder().request(request).protocol(Protocol.HTTP_1_1).code(200)
            .message("audit fixture").body(response.toResponseBody("application/json".toMediaType())).build()
    }
    fun service(): GeminiApiService = Retrofit.Builder().baseUrl("https://audit.invalid/")
        .client(OkHttpClient.Builder().addInterceptor(this).build().also { clients.add(it) })
        .addConverterFactory(MoshiConverterFactory.create(moshi)).build().create(GeminiApiService::class.java)
}
private fun bind(agent: ResearchAgent, transport: CapturedTransport) {
    ResearchAgent::class.java.getDeclaredField("apiService").apply { isAccessible = true }.set(agent, transport.service())
}
private class MemoryDaos : ExperimentDao, LedgerDao, ConfigDao {
    val experiments = MutableStateFlow<List<ExperimentResult>>(emptyList())
    val configs = MutableStateFlow<List<ExperimentConfig>>(emptyList())
    val ledger = MutableStateFlow<List<LedgerEntry>>(emptyList())
    override fun getAllExperiments(): Flow<List<ExperimentResult>> = experiments
    override suspend fun getExperimentById(id: String) = experiments.value.find { it.experimentId == id }
    override suspend fun insertExperiment(experiment: ExperimentResult) { experiments.value += experiment }
    override suspend fun deleteExperimentById(id: String) { experiments.value = experiments.value.filterNot { it.experimentId == id } }
    override fun getAllEntries(): Flow<List<LedgerEntry>> = ledger
    override suspend fun insertEntry(entry: LedgerEntry) { ledger.value += entry }
    override fun getAllConfigs(): Flow<List<ExperimentConfig>> = configs
    override suspend fun insertConfig(config: ExperimentConfig) { configs.value = configs.value.filterNot { it.id == config.id } + config }
    override suspend fun deleteConfigById(id: String) { configs.value = configs.value.filterNot { it.id == id } }
}
private suspend fun waitUntil(predicate: () -> Boolean) = withTimeout(10000) {
    while (!predicate()) delay(5)
}

fun main() = runBlocking {
 System.setSecurityManager(NoNetwork())
 check("A4-LEM-NETWORK-DENY", "control", try { Socket("127.0.0.1",1); false } catch (_: SecurityException) {true})
 ApiKeyStore.init(Context())
 // Credentials below are explicit synthetic labels; no network transport is reachable.
 suspend fun embedding(change: String?): Pair<CapturedTransport,ExperimentResult> {
  ApiKeyStore.setGeminiApiKey("SYNTHETIC_ACCOUNT_A")
  val transport=CapturedTransport()
  transport.onRequest={ n -> if(n==1) {if(change=="clear") ApiKeyStore.clearGeminiApiKey() else if(change!=null) ApiKeyStore.setGeminiApiKey(change)} }
  val dao=MemoryDaos();val vm=ResearchViewModel(ResearchRepository(dao,dao,dao))
  val agent=ResearchViewModel::class.java.getDeclaredField("agent").apply{isAccessible=true}.get(vm) as ResearchAgent
  bind(agent,transport);vm.runEmbeddingInstrument("models/synthetic-embedding")
  waitUntil{dao.experiments.value.size==1&&vm.activeInstrument.value==null}
  return transport to dao.experiments.value.single()
 }
 val (stableT,stableR)=embedding(null)
 check("A4-LEM-CREDENTIAL-STABLE","contract", stableT.requests.size==2 && stableT.requests.all{it["credentialLabel"]=="SYNTHETIC_ACCOUNT_A"} && stableR.status=="INSTRUMENT_OK",mapOf("requests" to stableT.requests,"status" to stableR.status))
 val (changedT,changedR)=embedding("SYNTHETIC_ACCOUNT_B")
 val mixed=changedT.requests.map{it["credentialLabel"]}.distinct().size>1
 check("A4-LEM-001","A",mixed && changedR.status=="INSTRUMENT_OK",mapOf("requests" to changedT.requests,"status" to changedR.status,"provenance" to changedR.sourceProvenanceProbes))
 check("A4-LEM-001","B",!mixed && ((changedT.requests.size==2 && changedT.requests.all{it["credentialLabel"]=="SYNTHETIC_ACCOUNT_A"} && changedR.status=="INSTRUMENT_OK") || (changedT.requests.size==1 && changedR.status in listOf("INSTRUMENT_FAILED","INSTRUMENT_CANCELLED"))),mapOf("criterion" to "A running two-call experiment must not silently switch credential authority; snapshot or explicitly abort at change."))
 val (clearedT,clearedR)=embedding("clear")
 check("A4-LEM-REVOCATION","contract",clearedT.requests.size==1 && clearedR.status=="INSTRUMENT_FAILED",mapOf("calls" to clearedT.requests.size,"status" to clearedR.status))
 // Deterministic in-flight registry reply: transport has captured A before UI clear/rekey.
 suspend fun staleRegistry(rekey:Boolean) {
  ApiKeyStore.setGeminiApiKey("SYNTHETIC_ACCOUNT_A")
  val dao=MemoryDaos(); val vm=ResearchViewModel(ResearchRepository(dao,dao,dao))
  val agent=ResearchViewModel::class.java.getDeclaredField("agent").apply{isAccessible=true}.get(vm) as ResearchAgent
  val entered=java.util.concurrent.CountDownLatch(1); val release=java.util.concurrent.CountDownLatch(1)
  val t=CapturedTransport().apply{mode="models";onRequest={entered.countDown();check(release.await(10,java.util.concurrent.TimeUnit.SECONDS))}}
  bind(agent,t);vm.refreshModels()
  check(entered.await(10,java.util.concurrent.TimeUnit.SECONDS))
  if(rekey) vm.saveApiKey("SYNTHETIC_ACCOUNT_B") else vm.clearApiKey()
  release.countDown();waitUntil{!vm.isModelRefreshRunning.value}
  val stale=vm.models.value.isNotEmpty()
  check("A4-LEM-002-"+(if(rekey)"REKEY" else "CLEAR"),"A",stale && t.requests.size==1,mapOf("models" to vm.models.value.map{it.name},"requestCredentials" to t.requests.map{it["credentialLabel"]},"configured" to vm.apiKeyConfigured.value,"message" to vm.instrumentMessage.value))
  check("A4-LEM-002-"+(if(rekey)"REKEY" else "CLEAR"),"B",!stale || (rekey && t.requests.last()["credentialLabel"]=="SYNTHETIC_ACCOUNT_B"),mapOf("criterion" to "Response for superseded credential epoch cannot repopulate current registry; rekey requires current reply or explicit unavailable state."))
 }
 staleRegistry(false);staleRegistry(true)
 println(json.toJson(mapOf("schema" to "klbt.audit.pass4.lem/1","tests" to tests,"boundaries" to listOf("Production Kotlin methods, Retrofit/Moshi/OkHttp execute unchanged","Application interceptor captures all requests and never proceeds; JVM denies sockets","Android preferences/lifecycle and DAO are host fixtures, no Room/device persistence claim","Synthetic payloads are mechanics fixtures, not scientific measurements"))))
 clients.forEach{it.dispatcher.executorService.shutdown();it.connectionPool.evictAll()}
 Unit
}
