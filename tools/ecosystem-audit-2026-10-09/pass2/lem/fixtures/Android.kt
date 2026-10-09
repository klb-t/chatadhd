package android.content

// Test infrastructure only: private SharedPreferences in memory, no Android claims.
open class Context {
    companion object { const val MODE_PRIVATE: Int = 0 }
    val applicationContext: Context get() = this
    private val preferences = mutableMapOf<String, SharedPreferences>()
    fun getSharedPreferences(name: String, mode: Int): SharedPreferences =
        preferences.getOrPut(name) { SharedPreferences() }
}
class SharedPreferences {
    private val values = mutableMapOf<String, String>()
    fun getString(key: String, default: String?): String? = values[key] ?: default
    fun edit(): Editor = Editor(values)
    class Editor(private val values: MutableMap<String, String>) {
        fun putString(key: String, value: String): Editor { values[key] = value; return this }
        fun remove(key: String): Editor { values.remove(key); return this }
        fun apply() {}
    }
}
