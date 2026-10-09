package androidx.lifecycle
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob

// Lifecycle seam only. Coroutines/Flow and the application's ViewModel are real.
open class ViewModel {
    internal val auditScope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
}
val ViewModel.viewModelScope: CoroutineScope get() = auditScope
