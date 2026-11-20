package com.murdoc820.hyperplay.update

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
}
