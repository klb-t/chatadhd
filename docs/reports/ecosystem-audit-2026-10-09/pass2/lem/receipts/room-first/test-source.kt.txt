package audit.lem.room

import android.content.Context
import android.database.sqlite.SQLiteDatabase
import androidx.room.Database
import androidx.room.Room
import androidx.room.RoomDatabase
import com.example.data.local.AppDatabase
import com.example.data.local.ExperimentDao
import com.example.data.local.LedgerDao
import com.example.data.model.ExperimentConfig
import com.example.data.model.ExperimentResult
import com.example.data.model.LedgerEntry
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.runBlocking
import org.junit.After
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config

// Schema fixture reproduces @Database entities/version from historical
// 0c6ea26abecd7c401dd9cb452309741216533c30. Both entity files are byte-identical
// at audited main. Real Room/KSP generates SQL; no hand-copied migration logic.
@Database(entities=[ExperimentResult::class, LedgerEntry::class], version=1, exportSchema=false)
abstract class HistoricalV1 : RoomDatabase() {
    abstract fun experimentDao(): ExperimentDao
    abstract fun ledgerDao(): LedgerDao
}

@RunWith(RobolectricTestRunner::class)
@Config(sdk=[28])
class RoomPersistenceAuditTest {
    private val context: Context get() = RuntimeEnvironment.getApplication()
    private fun resetSingleton() {
        val f=AppDatabase::class.java.getDeclaredField("INSTANCE").apply { isAccessible=true }
        (f.get(null) as AppDatabase?)?.close()
        f.set(null,null)
    }
    @Before fun setup() { resetSingleton(); context.deleteDatabase("lemlab_database") }
    @After fun cleanup() { resetSingleton(); context.deleteDatabase("lemlab_database") }

    private fun createV1() = runBlocking {
        val v1=Room.databaseBuilder(context,HistoricalV1::class.java,"lemlab_database").build()
        v1.ledgerDao().insertEntry(LedgerEntry(claim="synthetic historical fixture", evidence="audit",counterevidence="",status="fixture",experimentIds="[]",confidence="unknown",openQuestions=""))
        assertEquals(1,v1.ledgerDao().getAllEntries().first().size)
        v1.close()
    }

    @Test fun A_LEM005_reproducesHistoricalLedgerDeletion() = runBlocking {
        createV1()
        val actual=AppDatabase.getDatabase(context)
        assertEquals("A PASS means destructive migration reproduced",0,actual.ledgerDao().getAllEntries().first().size)
    }

    @Test fun B_LEM005_preservesHistoricalLedger() = runBlocking {
        createV1()
        val actual=AppDatabase.getDatabase(context)
        val rows=actual.ledgerDao().getAllEntries().first()
        assertEquals("Acceptance requires the historical row",1,rows.size)
        assertEquals("synthetic historical fixture",rows.single().claim)
    }

    @Test fun B_configRoundtripThroughActualRoomAfterCloseReopen() = runBlocking {
        val selected=ExperimentConfig("audit-version-1","synthetic fixture",embeddingDimension=256,embeddingTaskType="RETRIEVAL_QUERY",epochs=3)
        AppDatabase.getDatabase(context).configDao().insertConfig(selected)
        resetSingleton()
        val restored=AppDatabase.getDatabase(context).configDao().getAllConfigs().first().single()
        assertEquals(selected,restored)
    }

    @Test fun B_unknownVersionFailsWithoutDeletingRows() = runBlocking {
        createV1()
        SQLiteDatabase.openDatabase(context.getDatabasePath("lemlab_database").path,null,SQLiteDatabase.OPEN_READWRITE).use { it.version=99 }
        var refused=false
        try { AppDatabase.getDatabase(context).ledgerDao().getAllEntries().first() }
        catch (_: IllegalStateException) { refused=true }
        resetSingleton()
        val rows=SQLiteDatabase.openDatabase(context.getDatabasePath("lemlab_database").path,null,SQLiteDatabase.OPEN_READONLY).use { db ->
            db.rawQuery("SELECT COUNT(*) FROM ledger_entries",null).use { cursor -> cursor.moveToFirst();cursor.getInt(0) }
        }
        assertTrue("Unsupported version must be refused explicitly",refused)
        assertEquals("Unknown-version rejection must preserve data",1,rows)
    }
}
