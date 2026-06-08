import { Bluetooth, Cable, CheckCircle2, Printer, Settings2, Wifi } from "lucide-react";
import type { Equipment } from "@labconnect/shared-types";
import { StatusBadge } from "./status-badge";

export function DeviceSettingsPanel({ equipment }: { equipment: Equipment[] }) {
  return (
    <section className="rounded-lg border border-border bg-surface p-4 shadow-card">
      <div className="flex items-center justify-between gap-3">
        <div>
          <h2 className="text-base font-semibold text-slate-950">Paramètres appareils</h2>
          <p className="text-sm text-slate-500">Combo balance + imprimante du poste.</p>
        </div>
        <Settings2 className="h-5 w-5 text-slate-400" />
      </div>
      <div className="mt-4 space-y-3">
        {equipment.map((item) => (
          <article key={item.id} className="rounded-lg border border-border bg-surface-soft p-3">
            <div className="flex items-start justify-between gap-3">
              <div className="min-w-0">
                <p className="truncate text-sm font-semibold text-slate-950">{item.name}</p>
                <p className="mt-1 text-xs text-slate-500">{item.customerId} · SN: {item.serialNumber}</p>
              </div>
              <StatusBadge status={item.status} />
            </div>
            <div className="mt-3 grid gap-2 text-xs font-medium text-slate-600">
              <SettingRow icon={item.connection?.protocol === "wifi" ? Wifi : item.connection?.protocol === "bluetooth" ? Bluetooth : Cable} label="Connexion" value={item.connection?.label ?? "À configurer"} />
              <SettingRow icon={item.type === "terminal" ? Printer : CheckCircle2} label={item.type === "terminal" ? "Impression" : "Lecture poids"} value={item.type === "terminal" ? "Imprimante LabConnect · 58/80 mm" : "Balance connectée"} />
            </div>
          </article>
        ))}
      </div>
    </section>
  );
}

function SettingRow({ icon: Icon, label, value }: { icon: typeof Settings2; label: string; value: string }) {
  return (
    <div className="flex items-center justify-between gap-3">
      <span className="inline-flex items-center gap-1.5 text-slate-500">
        <Icon className="h-3.5 w-3.5" />
        {label}
      </span>
      <span className="truncate text-right font-semibold text-slate-800">{value}</span>
    </div>
  );
}
