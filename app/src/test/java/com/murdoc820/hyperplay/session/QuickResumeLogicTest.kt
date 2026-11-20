package com.murdoc820.hyperplay.session

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class QuickResumeLogicTest {
    private val hk = "/storage/emulated/0/PC games/Hollow Knight/hollow_knight.exe"
    private val cup = "/storage/emulated/0/PC games/Cuphead/Cuphead.exe"

    @Test
    fun nothingHeldLaunches() {
        assertEquals(QuickResumeAction.LAUNCH, QuickResumeLogic.action(null, false, hk))
    }

    @Test
    fun deadHeldGameLaunches() {
        assertEquals(QuickResumeAction.LAUNCH, QuickResumeLogic.action(hk, false, hk))
    }

    @Test
    fun sameGameResumes() {
        assertEquals(QuickResumeAction.RESUME, QuickResumeLogic.action(hk, true, hk))
    }

    @Test
    fun sdcardAliasCountsAsSameGame() {
        val alias = "/sdcard/PC games/Hollow Knight/hollow_knight.exe"
        assertTrue(QuickResumeLogic.sameExe(alias, hk))
        assertEquals(QuickResumeAction.RESUME, QuickResumeLogic.action(hk, true, alias))
    }

    @Test
    fun otherGameKillsHeldFirst() {
        assertFalse(QuickResumeLogic.sameExe(hk, cup))
        assertEquals(QuickResumeAction.KILL_AND_LAUNCH, QuickResumeLogic.action(hk, true, cup))
    }

    @Test
    fun quickMenuActionsComeFirst() {
        val items = QuickMenuItem.entries
        // Panel izquierdo (acciones) y luego los ajustes; el menú se abre en Reanudar (índice 0).
        assertEquals(listOf(QuickMenuItem.RESUME, QuickMenuItem.QUICK_EXIT, QuickMenuItem.FORCE_KILL), items.take(3))
        assertTrue(items.take(3).all { it.isAction })
        assertTrue(items.drop(3).none { it.isAction })
    }
}
