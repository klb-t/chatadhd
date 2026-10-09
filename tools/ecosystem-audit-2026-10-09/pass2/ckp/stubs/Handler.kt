package android.os
class Looper {companion object {fun getMainLooper()=Looper()}}
class Handler(looper:Looper) {
 fun post(action:()->Unit){pending+=action}
 companion object {private val pending=mutableListOf<()->Unit>();fun drain(){val copy=pending.toList();pending.clear();copy.forEach{it()}}}
}
