package android.content
class Assets(val raw: String) { fun open(name:String)=raw.byteInputStream() }
open class Context(val assets:Assets=Assets("[BROKEN")) { val applicationContext:Context get()=this }
