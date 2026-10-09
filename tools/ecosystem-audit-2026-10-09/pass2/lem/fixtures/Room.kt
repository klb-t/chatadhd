package androidx.room

import android.content.Context
import kotlin.reflect.KClass

annotation class Entity(val tableName: String)
annotation class PrimaryKey(val autoGenerate: Boolean = false)
annotation class Dao
annotation class Query(val value: String)
annotation class Insert(val onConflict: Int)
object OnConflictStrategy { const val REPLACE: Int = 1 }
annotation class Database(val entities: Array<KClass<*>>, val version: Int, val exportSchema: Boolean)
abstract class RoomDatabase

// Transparent call spy, NOT a Room migration/storage implementation.
object Room {
    var destructive = false
    var migrations = 0
    var requestedName = ""
    lateinit var instance: RoomDatabase
    fun <T: RoomDatabase> databaseBuilder(context: Context, type: Class<T>, name: String): Builder<T> {
        requestedName = name
        return Builder()
    }
    class Builder<T: RoomDatabase> {
        fun fallbackToDestructiveMigration(): Builder<T> { destructive = true; return this }
        fun addMigrations(vararg ignored: Any): Builder<T> { migrations += ignored.size; return this }
        @Suppress("UNCHECKED_CAST") fun build(): T = instance as T
    }
}
