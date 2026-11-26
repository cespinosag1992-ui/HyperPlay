package com.murdoc820.hyperplay.update

import android.content.pm.PackageInstaller
import com.murdoc820.hyperplay.R
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class AppUpdaterTest {
    @Test
    fun comparesVersionsNumerically() {
        assertTrue(AppUpdater.isNewer("1.0.1", "1.0.0"))
        assertTrue(AppUpdater.isNewer("1.10.0", "1.9.9"))
        assertTrue(AppUpdater.isNewer("2.0", "1.99.99"))
        assertFalse(AppUpdater.isNewer("1.0.0", "1.0.0"))
        assertFalse(AppUpdater.isNewer("1.0.0", "1.0.1"))
        assertFalse(AppUpdater.isNewer("1.2.0-beta", "1.2.0"))
    }

    @Test
    fun picksTheOfficialReleaseApk() {
        val official = "HyperPlay-v1.2.0-Snapdragon8Elite-arm64-v8a.apk"
        // The .sha256 file and other APKs must not win over the official name, whatever the order.
        assertEquals(2, AppUpdater.pickApkAsset(listOf("$official.sha256", "other.apk", official)))
        assertEquals(0, AppUpdater.pickApkAsset(listOf(official, "$official.sha256")))
        assertEquals(1, AppUpdater.pickApkAsset(listOf("x86.apk", "hp-arm64.apk")))
        assertEquals(0, AppUpdater.pickApkAsset(listOf("app-release.apk", "notes.txt")))
        assertEquals(-1, AppUpdater.pickApkAsset(listOf("$official.sha256", "runtime.zip")))
        assertEquals(-1, AppUpdater.pickApkAsset(emptyList()))
        // After the rename to Exedra64 the official APK keeps winning.
        val renamed = "Exedra64-v1.2.0-Snapdragon8Elite-arm64-v8a.apk"
        assertEquals(1, AppUpdater.pickApkAsset(listOf("other.apk", renamed, "$renamed.sha256")))
    }

    @Test
    fun mapsInstallerFailuresToClearMessages() {
        assertEquals(R.string.update_err_aborted, AppUpdater.failureReason(PackageInstaller.STATUS_FAILURE_ABORTED))
        assertEquals(R.string.update_err_conflict, AppUpdater.failureReason(PackageInstaller.STATUS_FAILURE_CONFLICT))
        assertEquals(R.string.update_err_blocked, AppUpdater.failureReason(PackageInstaller.STATUS_FAILURE_BLOCKED))
        assertEquals(R.string.update_err_invalid, AppUpdater.failureReason(PackageInstaller.STATUS_FAILURE_INVALID))
        assertEquals(R.string.update_err_storage, AppUpdater.failureReason(PackageInstaller.STATUS_FAILURE_STORAGE))
        assertEquals(R.string.update_err_unknown, AppUpdater.failureReason(PackageInstaller.STATUS_FAILURE))
    }
}
