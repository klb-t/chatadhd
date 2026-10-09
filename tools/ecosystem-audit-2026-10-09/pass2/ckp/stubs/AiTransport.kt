package com.example.core.ai
import kotlinx.coroutines.CompletableDeferred
import com.example.core.config.Settings
enum class AiRequestTask { REWRITE }
class AiConfig {companion object { fun from(s:Settings,task:AiRequestTask)=AiConfig() }}
object AiClient {
 var response=CompletableDeferred<Result<String>>()
 var capturedSystem:String?=null
 var capturedUser:String?=null
 suspend fun complete(config:AiConfig,systemPrompt:String,userPrompt:String):Result<String> {
  capturedSystem=systemPrompt;capturedUser=userPrompt;return response.await()
 }
 fun reset(){response=CompletableDeferred();capturedSystem=null;capturedUser=null}
}
