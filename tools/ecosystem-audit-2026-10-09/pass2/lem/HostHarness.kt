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
    var failureMessage = "synthetic transport failure"
    override fun intercept(chain: Interceptor.Chain): Response {
        val request = chain.request()
        val b = Buffer(); request.body?.writeTo(b)
        requests.add(mapOf("method" to request.method, "path" to request.url.encodedPath,
            "pageToken" to request.url.queryParameter("pageToken"),
            "body" to b.readUtf8().let { if (it.isEmpty()) null else json.fromJson(it) }))
        val response = when(mode) {
            "partial_failure" -> if (requests.size == 2) throw IOException("synthetic second-step timeout") else "{\"embedding\":{\"values\":[1.0,2.0,3.0]}}"
            "tab_failure" -> throw IOException("synthetic\ttransport failure")
            "control_failure" -> throw IOException(failureMessage)
            "generation" -> "{\"candidates\":[{\"content\":{\"parts\":[{\"text\":\"  LEM_LAB_GENERATION_OK  \"}]}},{\"content\":{\"parts\":[{\"text\":\"SECOND_SYNTHETIC_CANDIDATE\"}]}}]}"
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
private suspend fun profileRun(profile: ExperimentConfig): Pair<List<Map<String, Any?>>, ExperimentResult> {
    val dao = MemoryDaos()
    val repo = ResearchRepository(dao, dao, dao)
    repo.insertConfig(profile)
    val vm = ResearchViewModel(repo)
    val agent = ResearchViewModel::class.java.getDeclaredField("agent").apply { isAccessible = true }.get(vm) as ResearchAgent
    val transport = CapturedTransport()
    bind(agent, transport)
    vm.runEmbeddingInstrument(profile.embeddingModel)
    waitUntil { dao.experiments.value.size == 1 && vm.activeInstrument.value == null }
    return transport.requests.toList() to dao.experiments.value.single()
}

fun main() = runBlocking {
    System.setSecurityManager(NoNetwork())
    val blocked = try { Socket("127.0.0.1", 1); false } catch (_: SecurityException) { true }
    check("LEM-HOST-NETWORK-DENY", "control", blocked)
    ApiKeyStore.init(Context())
    ApiKeyStore.setGeminiApiKey("AUDIT_SYNTHETIC_NONCREDENTIAL")

    val profileA = ExperimentConfig("audit-A", "A", embeddingDimension = 256, embeddingTaskType = "RETRIEVAL_QUERY", epochs = 3)
    val profileB = ExperimentConfig("audit-B", "B", embeddingDimension = 1024, embeddingTaskType = "CLASSIFICATION", epochs = 91)
    val (requestsA, resultA) = profileRun(profileA)
    val (requestsB, resultB) = profileRun(profileB)
    val bodyA = requestsA[0]["body"] as Map<*, *>
    val bodyB = requestsB[0]["body"] as Map<*, *>
    val effective = mapOf("savedProfileA" to mapOf("dimension" to 256, "taskType" to profileA.embeddingTaskType, "epochs" to 3),
        "savedProfileB" to mapOf("dimension" to 1024, "taskType" to profileB.embeddingTaskType, "epochs" to 91),
        "requestA" to bodyA, "requestB" to bodyB, "resultAStatus" to resultA.status, "resultBStatus" to resultB.status)
    check("LEM-001-PROFILES", "A", bodyA == bodyB && bodyA["outputDimensionality"] == 768.0 && bodyA["taskType"] == null, effective)
    check("LEM-001-PROFILES", "B", bodyA["outputDimensionality"] == 256.0 && bodyB["outputDimensionality"] == 1024.0 && bodyA["taskType"] == profileA.embeddingTaskType && bodyB["taskType"] == profileB.embeddingTaskType)
    val stored = resultA.fullHyperparameters + resultA.rawMetrics + resultA.sourceProvenanceProbes
    check("LEM-001-UNSUPPORTED", "A", resultA.status == "INSTRUMENT_OK" && !stored.contains("epochs"), mapOf("unsupportedField" to "epochs", "observableDisposition" to "absent"))
    check("LEM-001-UNSUPPORTED", "B", stored.contains("epochs") && (stored.contains("unsupported") || stored.contains("rejected")))
    check("LEM-001-CONFIG-PROVENANCE", "B", stored.contains(profileA.id), mapOf("profileIdInPersistedResult" to stored.contains(profileA.id)))

    val generationTransport = CapturedTransport().apply { mode = "generation" }
    val generationAgent = ResearchAgent().also { bind(it, generationTransport) }
    val generation = ExperimentRunner(generationAgent).runGenerationSmokeTest("models/audit-generation")
    check("LEM-003-RAW", "A", generation.filesArtifactsProduced == "[]" && !generation.rawMetrics.contains("SECOND_SYNTHETIC_CANDIDATE"),
        mapOf("artifactReferences" to generation.filesArtifactsProduced, "rawMetrics" to generation.rawMetrics, "responseCandidates" to 2, "request" to generationTransport.requests))
    check("LEM-003-RAW", "B", generation.filesArtifactsProduced != "[]")
    tests.add(mapOf("id" to "LEM-003-ARTIFACT-ROUNDTRIP", "phase" to "B", "status" to "BLOCKED", "reason" to "No artifact store/resolver contract in baseline; nonempty references are only a necessary gate, never proof of retrievable raw bytes"))

    val partialTransport = CapturedTransport().apply { mode = "partial_failure" }
    val partialAgent = ResearchAgent().also { bind(it, partialTransport) }
    val partial = ExperimentRunner(partialAgent).runEncoderSmokeTest("models/audit-embedding", 256)
    check("LEM-003-PARTIAL", "A", partialTransport.requests.size == 2 && partial.filesArtifactsProduced == "[]" && partial.fullHyperparameters == "{}",
        mapOf("transportCalls" to partialTransport.requests.size, "artifactReferences" to partial.filesArtifactsProduced, "hyperparameters" to partial.fullHyperparameters, "status" to partial.status))
    check("LEM-003-PARTIAL", "B", partial.filesArtifactsProduced != "[]" && partial.fullHyperparameters.contains("256"))
    check("LEM-008-FAILURE-STATUS", "B", partial.status == "INSTRUMENT_FAILED")

    val modelTransport = CapturedTransport().apply { mode = "models" }
    val modelAgent = ResearchAgent().also { bind(it, modelTransport) }
    val models = modelAgent.listModels().map { it.name }
    check("LEM-004-PAGINATION", "A", models == listOf("models/page-1") && modelTransport.requests.size == 1,
        mapOf("models" to models, "requests" to modelTransport.requests, "serverNextPageToken" to "audit-page-2"))
    check("LEM-004-PAGINATION", "B", models.toSet() == setOf("models/page-1", "models/page-2") && modelTransport.requests.any { it["pageToken"] == "audit-page-2" })

    val dao = MemoryDaos()
    Room.instance = object : AppDatabase() {
        override fun configDao(): ConfigDao = dao
        override fun experimentDao(): ExperimentDao = dao
        override fun ledgerDao(): LedgerDao = dao
    }
    AppDatabase.getDatabase(Context())
    check("LEM-005-DESTRUCTIVE-BUILDER", "A", Room.destructive && Room.migrations == 0,
        mapOf("fallbackToDestructiveMigrationCalled" to Room.destructive, "addMigrationsCalls" to Room.migrations, "scope" to "real AppDatabase against Room call spy; no database migration executed"))
    check("LEM-005-DESTRUCTIVE-BUILDER", "B", !Room.destructive)
    tests.add(mapOf("id" to "LEM-005-V1-V2-DATA-PRESERVATION", "phase" to "B", "status" to "BLOCKED", "reason" to "Requires real Room generated implementation + Android SQLite; not represented by Room call spy"))

    val controls = (0..31).map { code ->
        val transport = CapturedTransport().apply { mode = "control_failure"; failureMessage = "synthetic${code.toChar()}transport failure" }
        val agent = ResearchAgent().also { bind(it, transport) }
        val result = ExperimentRunner(agent).runGenerationSmokeTest("models/audit-generation")
        mapOf("codepoint" to code, "syntheticRawMetrics" to result.rawMetrics)
    }
    check("LEM-009-JSON-CONTROL-CHAR", "A", true, mapOf("strictDecoderInputs" to controls))
    check("LEM-009-JSON-CONTROL-CHAR", "B", false)

    ApiKeyStore.clearGeminiApiKey()
    val deniedTransport = CapturedTransport()
    val deniedAgent = ResearchAgent().also { bind(it, deniedTransport) }
    val denied = ExperimentRunner(deniedAgent).runEncoderSmokeTest("models/audit")
    check("LEM-008-NO-KEY-NO-SEND", "B", deniedTransport.requests.isEmpty() && denied.status == "INSTRUMENT_FAILED")
    println(json.toJson(mapOf("schema" to "klbt.audit.lem-host/1", "tests" to tests,
        "boundaries" to mapOf("productionSources" to "ExperimentRunner, ResearchAgent, GeminiApiService, ApiKeyStore, ResearchRepository, ResearchViewModel, AppDatabase, three models and DAOs",
            "realLibraries" to "Kotlin/coroutines/Flow/Retrofit/Moshi/OkHttp",
            "testSeams" to "Android Context/SharedPreferences, lifecycle scope, Room call spy, in-memory DAOs, serialization annotation",
            "network" to "application interceptor never calls proceed; JVM SecurityManager denies all socket connects",
            "notTested" to "Room persistence/migration, Android scheduling/lifecycle, Compose UI, device and APK"))))
    clients.forEach { it.dispatcher.executorService.shutdown(); it.connectionPool.evictAll() }
    Unit
}
