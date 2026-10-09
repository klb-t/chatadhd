package com.example.core.config
// Infrastructure fixture: only immutable input slots used by these real consumers.
data class Settings(val customProvidersJson:String="[]",val fetchedProvidersJson:String="[]",val aiEnabled:Boolean=true,val aiCustomTasksJson:String="[]",val convertLocalOnly:Boolean=true,val expertMode:Boolean=true)
object SettingsStore { var current=Settings();fun update(f:(Settings)->Settings) {current=f(current)} }
object Knobs {const val SCREEN_CONTEXT_CHARS="screenContextChars"}
fun Settings.knobInt(key:String)=1024
