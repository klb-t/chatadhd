package com.example.util
object AppLogger { val errors= mutableListOf<String>(); fun e(tag:String,msg:String,e:Throwable){ errors+=msg } }
