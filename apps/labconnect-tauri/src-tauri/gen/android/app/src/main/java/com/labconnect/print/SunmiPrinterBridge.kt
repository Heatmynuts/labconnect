package com.labconnect.print

import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.ServiceConnection
import android.os.IBinder
import android.os.Parcel
import android.util.Log
import android.util.Base64
import android.webkit.JavascriptInterface
import org.json.JSONObject
import java.io.ByteArrayOutputStream
import java.nio.charset.StandardCharsets
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

class SunmiPrinterBridge(private val context: Context) {
  private val descriptor = "woyou.aidlservice.jiuiv5.IWoyouService"
  private val servicePackage = "woyou.aidlservice.jiuiv5"
  private val serviceAction = "woyou.aidlservice.jiuiv5.IWoyouService"
  private val readyLatch = CountDownLatch(1)
  @Volatile private var binder: IBinder? = null
  @Volatile private var isBound = false

  private val printerConnection = object : ServiceConnection {
    override fun onServiceConnected(name: ComponentName, service: IBinder) {
      binder = service
      isBound = true
      readyLatch.countDown()
      Log.i("LabConnectPrinter", "SUNMI printer service connected")
    }

    override fun onServiceDisconnected(name: ComponentName) {
      binder = null
      isBound = false
      Log.w("LabConnectPrinter", "SUNMI printer service disconnected")
    }
  }

  init {
    bind()
  }

  fun release() {
    if (isBound) {
      try {
        context.unbindService(printerConnection)
      } catch (_: Exception) {
      }
    }
    isBound = false
    binder = null
  }

  @JavascriptInterface
  fun printTicket(payload: String): String {
    return try {
      Log.i("LabConnectPrinter", "printTicket called")
      val printer = awaitPrinter()
      val ticket = buildTicket(JSONObject(payload))

      transact(printer, 4) { writeStrongBinder(null) } // printerInit
      transact(printer, 12) { writeInt(1); writeStrongBinder(null) } // setAlignment(center)
      transact(printer, 14) { writeFloat(28f); writeStrongBinder(null) } // setFontSize
      transact(printer, 11) { writeByteArray(byteArrayOf(0x1B, 0x45, 0x01)); writeStrongBinder(null) } // bold on
      transact(printer, 15) { writeString(ticket.title); writeStrongBinder(null) } // printText
      transact(printer, 11) { writeByteArray(byteArrayOf(0x1B, 0x45, 0x00)); writeStrongBinder(null) } // bold off

      transact(printer, 12) { writeInt(0); writeStrongBinder(null) } // setAlignment(left)
      transact(printer, 14) { writeFloat(22f); writeStrongBinder(null) }
      transact(printer, 15) { writeString(ticket.body); writeStrongBinder(null) }

      transact(printer, 12) { writeInt(1); writeStrongBinder(null) }
      ticket.qrPayload?.let { qrPayload ->
        transact(printer, 11) { writeByteArray(buildQrCode(qrPayload)); writeStrongBinder(null) }
        transact(printer, 10) { writeInt(1); writeStrongBinder(null) }
      }
      transact(printer, 14) { writeFloat(20f); writeStrongBinder(null) }
      transact(printer, 15) { writeString(ticket.footer); writeStrongBinder(null) }
      transact(printer, 10) { writeInt(4); writeStrongBinder(null) } // lineWrap

      """{"ok":true}"""
    } catch (error: Exception) {
      Log.e("LabConnectPrinter", "Print failed", error)
      JSONObject().put("ok", false).put("error", error.message ?: "Erreur impression").toString()
    }
  }

  private fun bind() {
    val intent = Intent().apply {
      setPackage(servicePackage)
      action = serviceAction
    }
    isBound = context.bindService(intent, printerConnection, Context.BIND_AUTO_CREATE)
    if (!isBound) {
      Log.w("LabConnectPrinter", "bindService returned false")
    }
  }

  private fun awaitPrinter(): IBinder {
    binder?.let { return it }
    if (!isBound) bind()
    readyLatch.await(2500, TimeUnit.MILLISECONDS)
    return binder ?: throw IllegalStateException("Imprimante LabConnect indisponible")
  }

  private fun transact(service: IBinder, transactionId: Int, writePayload: Parcel.() -> Unit) {
    val data = Parcel.obtain()
    val reply = Parcel.obtain()
    try {
      data.writeInterfaceToken(descriptor)
      data.writePayload()
      val ok = service.transact(IBinder.FIRST_CALL_TRANSACTION + transactionId - 1, data, reply, 0)
      if (!ok) throw IllegalStateException("Transaction imprimante refusée: $transactionId")
      reply.readException()
    } finally {
      reply.recycle()
      data.recycle()
    }
  }

  private fun buildTicket(payload: JSONObject): TicketParts {
    val record = payload.optJSONObject("record") ?: JSONObject()
    val template = payload.optJSONObject("template") ?: JSONObject()
    val company = template.optString("companyName", "LABCONNECT")
    val title = template.optString("title", "TICKET DE PESEE")
    val footerText = template.optString("footer", "Signature operateur")
    val qrCodeEnabled = template.optBoolean("qrCodeEnabled", false)
    val lineWidth = if (template.optString("paperWidth", "80mm") == "58mm") 32 else 48
    val separator = "-".repeat(lineWidth)
    val customLines = template.optJSONArray("lines")
    val fields = template.optJSONArray("fields")
    val bodyLines = mutableListOf(separator)

    if (customLines != null && customLines.length() > 0) {
      for (index in 0 until customLines.length()) {
        val line = customTicketLine(customLines.optJSONObject(index), record, lineWidth)
        if (line != null) bodyLines.add(line)
      }
      bodyLines.add(separator)
      bodyLines.add("")
    } else if (fields == null || fields.length() == 0) {
      bodyLines.addAll(defaultTicketLines(record, separator, lineWidth))
    } else {
      for (index in 0 until fields.length()) {
        val field = fields.optString(index)
        val line = ticketFieldLine(field, record, lineWidth)
        if (line != null) bodyLines.add(line)
      }
      bodyLines.add(separator)
      bodyLines.add("")
    }

    return TicketParts(
      title = "$company\n$title\n\n",
      body = bodyLines.joinToString("\n"),
      footer = "$footerText\n\n",
      qrPayload = if (qrCodeEnabled) buildQrPayload(record) else null
    )
  }

  private fun defaultTicketLines(record: JSONObject, separator: String, lineWidth: Int): List<String> {
    val raw = record.optString("rawBalanceLine", record.optString("weight", "-"))
    return listOf(
      ticketLine("Date", recordValue(record, "dateTime", SimpleDateFormat("dd/MM/yyyy HH:mm", Locale.FRANCE).format(Date())), lineWidth),
      ticketLine("Operateur", recordValue(record, "operator"), lineWidth),
      ticketLine("Lot", recordValue(record, "lotNumber"), lineWidth),
      ticketLine("Echantillon", recordValue(record, "sampleId"), lineWidth),
      separator,
      ticketLine("Poids", raw, lineWidth),
      ticketLine("Brut", recordValue(record, "grossWeight", raw), lineWidth),
      ticketLine("Tare", recordValue(record, "tareWeight"), lineWidth),
      ticketLine("Net", recordValue(record, "netWeight", raw), lineWidth),
      separator,
      ""
    )
  }

  private fun ticketFieldLine(field: String, record: JSONObject, lineWidth: Int): String? {
    val raw = record.optString("rawBalanceLine", record.optString("weight", "-"))
    return when (field) {
      "dateTime" -> ticketLine("Date", recordValue(record, "dateTime", SimpleDateFormat("dd/MM/yyyy HH:mm", Locale.FRANCE).format(Date())), lineWidth)
      "operator" -> ticketLine("Operateur", recordValue(record, "operator"), lineWidth)
      "balanceName" -> ticketLine("Balance", recordValue(record, "balanceName"), lineWidth)
      "rawBalanceLine" -> ticketLine("Ligne brute", raw, lineWidth)
      "weight" -> ticketLine("Poids", recordValue(record, "weight", raw), lineWidth)
      "grossWeight" -> ticketLine("Brut", recordValue(record, "grossWeight", raw), lineWidth)
      "tareWeight" -> ticketLine("Tare", recordValue(record, "tareWeight"), lineWidth)
      "netWeight" -> ticketLine("Net", recordValue(record, "netWeight", raw), lineWidth)
      "unit" -> ticketLine("Unite", recordValue(record, "unit"), lineWidth)
      "lotNumber" -> ticketLine("Lot", recordValue(record, "lotNumber"), lineWidth)
      "sampleId" -> ticketLine("Echantillon", recordValue(record, "sampleId"), lineWidth)
      "methodName" -> ticketLine("Methode", recordValue(record, "methodName"), lineWidth)
      "dryingTemperature" -> ticketLine("Temperature", recordValue(record, "dryingTemperature"), lineWidth)
      "dryingTime" -> ticketLine("Duree", recordValue(record, "dryingTime"), lineWidth)
      "startWeight" -> ticketLine("Poids init.", recordValue(record, "startWeight"), lineWidth)
      "dryWeight" -> ticketLine("Poids sec", recordValue(record, "dryWeight"), lineWidth)
      "moistureContent" -> ticketLine("Humidite", recordValue(record, "moistureContent"), lineWidth)
      "resultStatus" -> ticketLine("Resultat", recordValue(record, "resultStatus"), lineWidth)
      "signature" -> signatureBlock()
      else -> null
    }
  }

  private fun signatureBlock(): String {
    return listOf("Signature", "", "", "", "").joinToString("\n")
  }

  private fun customTicketLine(line: JSONObject?, record: JSONObject, lineWidth: Int): String? {
    if (line == null || !line.optBoolean("enabled", true)) return null;
    val source = line.optString("source", "text")
    val label = line.optString("label", "")
    if (source == "blank") return ""
    if (source == "signature") return signatureBlock()
    if (source == "text") return line.optString("text", label)
    if (source == "command") return ticketLine(label, commandValue(line.optString("command", "?SN"), record), lineWidth)
    return ticketLine(label, ticketValue(source, record), lineWidth)
  }

  private fun commandValue(command: String, record: JSONObject): String {
    val normalized = normalizeCommand(command)
    val responses = record.optJSONObject("commandResponses")
    return responses?.optString(normalized, "En attente") ?: "En attente"
  }

  private fun normalizeCommand(command: String): String {
    val value = command.trim().uppercase(Locale.FRANCE)
    return if (value.startsWith("?")) value else "?$value"
  }

  private fun ticketValue(source: String, record: JSONObject): String {
    val raw = record.optString("rawBalanceLine", record.optString("weight", "-"))
    return when (source) {
      "dateTime" -> recordValue(record, "dateTime", SimpleDateFormat("dd/MM/yyyy HH:mm", Locale.FRANCE).format(Date()))
      "operator" -> recordValue(record, "operator")
      "balanceName" -> recordValue(record, "balanceName")
      "rawBalanceLine" -> raw
      "weight" -> recordValue(record, "weight", raw)
      "grossWeight" -> recordValue(record, "grossWeight", raw)
      "tareWeight" -> recordValue(record, "tareWeight")
      "netWeight" -> recordValue(record, "netWeight", raw)
      "unit" -> recordValue(record, "unit")
      "lotNumber" -> recordValue(record, "lotNumber")
      "sampleId" -> recordValue(record, "sampleId")
      "methodName" -> recordValue(record, "methodName")
      "dryingTemperature" -> recordValue(record, "dryingTemperature")
      "dryingTime" -> recordValue(record, "dryingTime")
      "startWeight" -> recordValue(record, "startWeight")
      "dryWeight" -> recordValue(record, "dryWeight")
      "moistureContent" -> recordValue(record, "moistureContent")
      "resultStatus" -> recordValue(record, "resultStatus")
      else -> "-"
    }
  }

  private fun ticketLine(label: String, value: String, lineWidth: Int): String {
    val cleanLabel = label.take(14)
    val cleanValue = value.replace("\n", " ").trim()
    val prefix = "$cleanLabel "
    val available = (lineWidth - prefix.length).coerceAtLeast(8)
    val clippedValue = if (cleanValue.length > available) cleanValue.takeLast(available) else cleanValue
    val spaces = " ".repeat((lineWidth - prefix.length - clippedValue.length).coerceAtLeast(1))
    return "$prefix$spaces$clippedValue"
  }

  private fun recordValue(record: JSONObject, key: String, fallback: String = "-"): String {
    val value = record.optString(key, fallback)
    return if (value.isBlank()) fallback else value
  }

  private fun buildQrPayload(record: JSONObject): String {
    val payload = JSONObject()
      .put("v", 1)
      .put("id", recordValue(record, "id", "ticket-${System.currentTimeMillis()}"))
      .put("dt", recordValue(record, "dateTime"))
      .put("op", recordValue(record, "operator"))
      .put("lot", recordValue(record, "lotNumber"))
      .put("sample", recordValue(record, "sampleId"))
      .put("balance", recordValue(record, "balanceName"))
      .put("raw", recordValue(record, "rawBalanceLine"))
      .put("weight", recordValue(record, "weight", recordValue(record, "rawBalanceLine")))
      .put("gross", recordValue(record, "grossWeight", ""))
      .put("tare", recordValue(record, "tareWeight", ""))
      .put("net", recordValue(record, "netWeight", ""))
      .put("unit", recordValue(record, "unit", "g"))
      .toString()

    return "LCPT1:" + Base64.encodeToString(payload.toByteArray(StandardCharsets.UTF_8), Base64.URL_SAFE or Base64.NO_WRAP or Base64.NO_PADDING)
  }

  private fun buildQrCode(value: String): ByteArray {
    val qrData = value.toByteArray(StandardCharsets.UTF_8)
    val storeLength = qrData.size + 3
    val output = ByteArrayOutputStream()

    output.write(byteArrayOf(0x1D, 0x28, 0x6B, 0x04, 0x00, 0x31, 0x41, 0x32, 0x00))
    output.write(byteArrayOf(0x1D, 0x28, 0x6B, 0x03, 0x00, 0x31, 0x43, 0x06))
    output.write(byteArrayOf(0x1D, 0x28, 0x6B, 0x03, 0x00, 0x31, 0x45, 0x31))
    output.write(byteArrayOf(0x1D, 0x28, 0x6B, (storeLength and 0xFF).toByte(), ((storeLength shr 8) and 0xFF).toByte(), 0x31, 0x50, 0x30))
    output.write(qrData)
    output.write(byteArrayOf(0x1D, 0x28, 0x6B, 0x03, 0x00, 0x31, 0x51, 0x30))

    return output.toByteArray()
  }

  private data class TicketParts(
    val title: String,
    val body: String,
    val footer: String,
    val qrPayload: String?
  )
}
