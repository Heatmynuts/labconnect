import { Download, FileText, Printer } from "lucide-react";
import type { WeighingRecord } from "@labconnect/shared-types";
import { Button } from "./button";

export function WeighingHistory({
  records,
  onExportCsv,
  onExportPdf
}: {
  records: WeighingRecord[];
  onExportCsv: () => void;
  onExportPdf: () => void;
}) {
  return (
    <section className="rounded-lg border border-border bg-surface p-4 shadow-card">
      <div className="flex flex-wrap items-start justify-between gap-3">
        <div>
          <h2 className="text-base font-semibold text-slate-950">Historique local</h2>
          <p className="text-sm text-slate-500">Pesées conservées sur l’imprimante LabConnect.</p>
        </div>
        <div className="flex gap-2">
          <Button variant="secondary" size="sm" onClick={onExportCsv}>
            <Download className="h-4 w-4" />
            CSV
          </Button>
          <Button variant="secondary" size="sm" onClick={onExportPdf}>
            <FileText className="h-4 w-4" />
            PDF
          </Button>
        </div>
      </div>
      <div className="mt-4 space-y-2">
        {records.map((record) => (
          <article key={record.id} className="flex items-center justify-between gap-3 rounded-lg border border-border bg-surface-soft p-3">
            <div className="min-w-0">
              <p className="truncate font-mono text-sm font-bold text-slate-950">{record.rawBalanceLine}</p>
              <p className="mt-1 text-xs text-slate-500">{record.dateTime} · {record.operator} · {record.lotNumber}</p>
            </div>
            <Button variant="ghost" size="icon" aria-label="Réimprimer">
              <Printer className="h-4 w-4" />
            </Button>
          </article>
        ))}
      </div>
    </section>
  );
}
