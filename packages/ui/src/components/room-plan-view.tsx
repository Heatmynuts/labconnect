import type { Equipment } from "@labconnect/shared-types";

export function RoomPlanView({ equipment }: { equipment: Equipment[] }) {
  return (
    <section className="rounded-lg border border-border bg-surface p-4 shadow-card">
      <div className="flex items-center justify-between">
        <h2 className="text-base font-semibold text-slate-950">Plan laboratoire</h2>
        <span className="text-xs font-semibold text-slate-500">Vue zones</span>
      </div>
      <div className="mt-4 grid min-h-[220px] grid-cols-2 gap-3 rounded-2xl bg-surface-soft p-3">
        {["Salle de pesée", "Étiquetage", "Préparation", "Contrôle qualité"].map((room, index) => (
          <div key={room} className="relative rounded-lg border border-border bg-white p-3">
            <p className="text-xs font-semibold text-slate-500">{room}</p>
            {equipment[index] && (
              <div className="absolute bottom-3 left-3 rounded-full bg-brand-soft px-2.5 py-1 text-xs font-semibold text-brand">
                {equipment[index].customerId}
              </div>
            )}
          </div>
        ))}
      </div>
    </section>
  );
}
