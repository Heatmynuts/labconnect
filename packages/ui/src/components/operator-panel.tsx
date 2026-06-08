import { UserRound } from "lucide-react";

export function OperatorPanel({
  operator,
  lotNumber,
  sampleId,
  onOperatorChange,
  onLotChange,
  onSampleChange
}: {
  operator: string;
  lotNumber: string;
  sampleId: string;
  onOperatorChange: (value: string) => void;
  onLotChange: (value: string) => void;
  onSampleChange: (value: string) => void;
}) {
  return (
    <section className="rounded-lg border border-border bg-surface p-4 shadow-card">
      <div className="flex items-center gap-2">
        <UserRound className="h-5 w-5 text-brand" />
        <h2 className="text-base font-semibold text-slate-950">Session opérateur</h2>
      </div>
      <div className="mt-4 grid gap-3">
        <Field label="Opérateur" value={operator} onChange={onOperatorChange} />
        <Field label="Lot" value={lotNumber} onChange={onLotChange} />
        <Field label="Échantillon" value={sampleId} onChange={onSampleChange} />
      </div>
    </section>
  );
}

function Field({ label, value, onChange }: { label: string; value: string; onChange: (value: string) => void }) {
  return (
    <label className="grid gap-1.5 text-sm font-semibold text-slate-700">
      {label}
      <input
        className="min-h-11 rounded-2xl border border-border bg-white px-4 text-sm font-medium text-slate-950 outline-none transition focus:border-brand focus:outline focus:outline-[3px] focus:outline-brand/30"
        value={value}
        onChange={(event) => onChange(event.target.value)}
      />
    </label>
  );
}
