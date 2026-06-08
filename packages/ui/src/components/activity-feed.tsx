import { CheckCircle2 } from "lucide-react";

export function ActivityFeed({ items }: { items: Array<{ id: string; title: string; detail: string; time: string }> }) {
  return (
    <div className="rounded-lg border border-border bg-surface p-4 shadow-card">
      <h2 className="text-base font-semibold text-slate-950">Activité récente</h2>
      <div className="mt-4 space-y-3">
        {items.map((item) => (
          <div key={item.id} className="flex gap-3">
            <CheckCircle2 className="mt-0.5 h-4 w-4 shrink-0 text-green-600" />
            <div className="min-w-0 flex-1">
              <p className="text-sm font-semibold text-slate-900">{item.title}</p>
              <p className="text-xs text-slate-500">{item.detail}</p>
            </div>
            <span className="text-xs font-medium text-slate-400">{item.time}</span>
          </div>
        ))}
      </div>
    </div>
  );
}
