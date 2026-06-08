export function ProductScreenPreview() {
  return (
    <div className="w-28 rounded-[14px] border border-white/20 bg-slate-950 p-2 text-white shadow-float">
      <p className="text-[8px] font-semibold text-white/58">A&D FZ-5000i</p>
      <p className="mt-1 font-mono text-lg font-bold leading-5 tracking-normal">1248.52 g</p>
      <div className="mt-2 rounded-lg bg-brand px-2 py-1 text-center text-[8px] font-bold">Capturer</div>
      <div className="mt-1 grid grid-cols-3 gap-1 text-[7px] font-semibold text-white/72">
        <span>Tare</span>
        <span>Zéro</span>
        <span>Print</span>
      </div>
    </div>
  );
}
