package com.example.core.io
// Tests use named arguments exclusively. Positional catalogue resolution is untested.
data class Param(val name:String)
data class VerbSpec(val params:List<Param>)
object Verbs {fun byId(id:String):VerbSpec?=null}
