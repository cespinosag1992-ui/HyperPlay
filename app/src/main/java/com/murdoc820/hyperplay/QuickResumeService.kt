package com.murdoc820.hyperplay

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.IBinder
import android.util.Log
import com.murdoc820.hyperplay.session.QuickResume

/**
 * Servicio en primer plano mientras hay un juego en sesión o suspendido (Quick Resume): evita que
 * Android mate el proceso (y con él el juego en memoria) al salir a otra app.
 * Se arranca al iniciar la sesión (con la actividad visible, como exige Android 12+).
 */
class QuickResumeService : Service() {

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (intent?.action == ACTION_KILL) {
            Log.i(TAG, "cerrar juego suspendido desde la notificación")
            Thread({ QuickResume.killHeld() }, "hyperplay-qr-kill").start()  // para audio y despausa
            stopForeground(STOP_FOREGROUND_REMOVE)
            stopSelf()
            return START_NOT_STICKY
        }
        val title = intent?.getStringExtra(EXTRA_TITLE).orEmpty().ifEmpty { "Juego" }
        val exe = intent?.getStringExtra(GameSessionActivity.EXTRA_EXE_PATH).orEmpty()
        try {
            startForeground(NOTIFICATION_ID, buildNotification(title, exe), ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE)
        } catch (e: RuntimeException) {
            // Android 12+ prohíbe arrancarlo desde segundo plano: el juego sigue, sin protección.
            Log.w(TAG, "no se pudo pasar a primer plano", e)
            stopSelf()
        }
        return START_NOT_STICKY
    }

    private fun buildNotification(title: String, exe: String): Notification {
        val nm = getSystemService(NotificationManager::class.java)
        if (nm.getNotificationChannel(CHANNEL) == null) {
            nm.createNotificationChannel(
                NotificationChannel(CHANNEL, "Juego en Quick Resume", NotificationManager.IMPORTANCE_LOW),
            )
        }
        val open = PendingIntent.getActivity(
            this, 0,
            Intent(this, GameSessionActivity::class.java)
                .putExtra(GameSessionActivity.EXTRA_EXE_PATH, exe)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT,
        )
        val kill = PendingIntent.getService(
            this, 1, Intent(this, QuickResumeService::class.java).setAction(ACTION_KILL),
            PendingIntent.FLAG_IMMUTABLE,
        )
        return Notification.Builder(this, CHANNEL)
            .setSmallIcon(R.mipmap.ic_launcher)
            .setContentTitle(title)
            .setContentText("En memoria: vuelve al instante donde lo dejaste")
            .setContentIntent(open)
            .setOngoing(true)
            .addAction(Notification.Action.Builder(null, "Cerrar juego", kill).build())
            .build()
    }

    companion object {
        private const val TAG = "HyperPlayQR"
        private const val CHANNEL = "quick_resume"
        private const val NOTIFICATION_ID = 820
        private const val ACTION_KILL = "com.murdoc820.hyperplay.action.QR_KILL"
        private const val EXTRA_TITLE = "com.murdoc820.hyperplay.extra.QR_TITLE"

        fun start(context: Context, title: String, exePath: String) {
            val i = Intent(context, QuickResumeService::class.java)
                .putExtra(EXTRA_TITLE, title)
                .putExtra(GameSessionActivity.EXTRA_EXE_PATH, exePath)
            runCatching { context.startForegroundService(i) }
                .onFailure { Log.w(TAG, "no se pudo arrancar el servicio", it) }
        }

        fun stop(context: Context) {
            context.stopService(Intent(context, QuickResumeService::class.java))
        }
    }
}
