package com.murdoc820.hyperplay

import android.app.Application
import com.murdoc820.hyperplay.art.GameArtRepository
import com.murdoc820.hyperplay.diag.Diagnostics

/** Inicializa el registro de errores (antes que nada) y el arte de la biblioteca. */
class HyperPlayApp : Application() {
    override fun onCreate() {
        super.onCreate()
        Diagnostics.init(this)
        GameArtRepository.init(this)
    }
}
