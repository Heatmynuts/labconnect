package com.labconnect.print

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent

class LabConnectBootReceiver : BroadcastReceiver() {
  override fun onReceive(context: Context, intent: Intent) {
    val action = intent.action ?: return
    val shouldLaunch =
      action == Intent.ACTION_BOOT_COMPLETED ||
        action == Intent.ACTION_LOCKED_BOOT_COMPLETED ||
        action == Intent.ACTION_MY_PACKAGE_REPLACED ||
        action == "android.intent.action.QUICKBOOT_POWERON"

    if (!shouldLaunch) return

    val launchIntent =
      Intent(context, MainActivity::class.java).apply {
        addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP)
        addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP)
      }

    context.startActivity(launchIntent)
  }
}
