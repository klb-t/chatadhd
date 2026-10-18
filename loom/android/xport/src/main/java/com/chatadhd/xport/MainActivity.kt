package com.chatadhd.xport

import android.app.Activity
import android.os.Bundle
import android.widget.ScrollView
import android.widget.TextView
import com.chaquo.python.Python

// First milestone: prove the Python engine runs on the device and show its version.
// Next: pick an export (SAF), run inventory across providers, show the unified timeline.
class MainActivity : Activity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val text = TextView(this).apply { textSize = 16f; setPadding(32, 32, 32, 32) }
        text.text = try {
            val py = Python.getInstance()
            "X-Port (Python ${py.getModule("sys").get("version")})"
        } catch (e: Exception) {
            "Python nie działa: ${e.message}"
        }
        setContentView(ScrollView(this).apply { addView(text) })
    }
}
