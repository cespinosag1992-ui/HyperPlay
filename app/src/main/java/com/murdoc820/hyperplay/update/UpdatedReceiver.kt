package com.murdoc820.hyperplay.update

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import com.murdoc820.hyperplay.MainActivity
import com.murdoc820.hyperplay.diag.Diagnostics

/** MY_PACKAGE_REPLACED: registra la actualización, limpia el .apk descargado y reabre el launcher. */
class UpdatedReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        if (intent.action != Intent.ACTION_MY_PACKAGE_REPLACED) return
        Diagnostics.init(context)
        Diagnostics.log("I", "update", "app actualizada a ${AppUpdater.currentVersion}")
        java.io.File(context.filesDir, "updates").listFiles()?.forEach { it.delete() }
        runCatching {
            context.startActivity(Intent(context, MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
        }
    }
}
