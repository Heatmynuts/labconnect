export function MetricCard({ label, value, tone = "neutral" }: { label: string; value: string; tone?: "neutral" | "success" | "warning" }) {
  const toneClass = tone === "success" ? "text-green-700" : tone === "warning" ? "text-amber-700" : "text-slate-950";
  return (
    <div className="rounded-lg border border-border bg-surface p-4 shadow-card">
      <p className="text-sm font-medium text-slate-500">{label}</p>
      <p className={`mt-2 text-2xl font-bold tracking-normal ${toneClass}`}>{value}</p>
    </div>
  );
}
