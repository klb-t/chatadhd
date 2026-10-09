package com.example.audit

import android.content.Context
import androidx.test.core.app.ApplicationProvider
import com.example.core.config.*
import com.example.core.ai.AiTasks
import kotlinx.coroutines.runBlocking
import org.json.JSONObject
import org.junit.Test
import org.junit.Assert.*
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk=[34])
class EcosystemAuditConsumerTest {
 @Test fun unsupportedGenericSettingAcceptance() {
  val before=SettingsStore.current
  val outcome=SettingsStore.setByKey("audit_nonexistent_setting",true)
  assertEquals(before,SettingsStore.current)
  assertTrue("Unknown setting must not report successful application",outcome.isFailure)
 }
 @Test fun profileRejectsUnknownValueAndVersion() {
  assertTrue(SettingsProfiles.parseImport("""{"audit_nonexistent_setting":true}""").isFailure)
  val p=SettingsProfile("audit","audit","",JSONObject().put("clipboardEnabled",false))
  val encoded=SettingsProfiles.encode(p).put("version",999)
  assertTrue(SettingsProfiles.parseImport(encoded.toString()).isFailure)
 }
 @Test fun levelsShareSameState() {
  val base=Settings(clipboardEnabled=false,keyGapDp=7f,aiApiKey="synthetic-local-marker")
  val levels=listOf(SettingsLevel.BASIC,SettingsLevel.ADVANCED,SettingsLevel.EXPERT)
  for (level in levels) {
   val current=SettingsHierarchy.selectLevel(base,level)
   assertFalse(current.clipboardEnabled)
   assertEquals(7f,current.keyGapDp)
   assertEquals(base.aiApiKey,current.aiApiKey)
  }
 }
 @Test fun profileRoundtripKeepsDisabledAndRejectsCredentialImport() {
  val base=Settings(clipboardEnabled=false,keyGapDp=7f)
  val p=SettingsProfiles.capture("audit",base,listOf("clipboardEnabled","keyGapDp","aiApiKey"))
  val restored=SettingsProfiles.parseImport(SettingsProfiles.encode(p).toString()).getOrThrow()
  val applied=SettingsProfiles.apply(Settings(aiApiKey="synthetic-existing-key"),restored).getOrThrow()
  assertFalse(applied.clipboardEnabled)
  assertEquals(7f,applied.keyGapDp)
  assertEquals("synthetic-existing-key",applied.aiApiKey)
  assertFalse(restored.values.has("aiApiKey"))
  assertTrue(SettingsProfiles.parseImport("""{"aiApiKey":"synthetic-injected-key"}""").isFailure)
 }
 @Test fun profileStorageReopenKeepsDisabled()=runBlocking {
  val context=ApplicationProvider.getApplicationContext<Context>()
  SettingsProfiles.init(context)
  val p=SettingsProfile("audit-reopen","audit-reopen","",JSONObject().put("clipboardEnabled",false))
  SettingsProfiles.save(p)
  val field=SettingsProfiles.javaClass.getDeclaredField("context").apply{isAccessible=true}
  field.set(SettingsProfiles,null)
  SettingsProfiles.init(context)
  val reopened=SettingsProfiles.all().single{it.id=="audit-reopen"}
  assertFalse(SettingsProfiles.apply(Settings(),reopened).getOrThrow().clipboardEnabled)
 }
 @Test fun taskCollisionAcceptance() {
  assertEquals("user-profile",AiTasks.byId("""[{"id":"fix","system":"user-profile"}]""","fix")!!.systemPrompt)
 }
}
