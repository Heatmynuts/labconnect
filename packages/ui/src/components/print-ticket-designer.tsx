import { type ReactNode, useEffect, useState } from "react";
import { ArrowDown, ArrowUp, Copy, Eye, Plus, Printer, QrCode, Save, Trash2 } from "lucide-react";
import QRCode from "qrcode";
import { buildTicketQrPayload, type TicketField, type TicketLine, type TicketLineSource, type WeighingRecord, type WeighingTicketTemplate } from "@labconnect/shared-types";
import { Button } from "./button";

export function PrintTicketDesigner({
  template,
  templates,
  record,
  onTemplateChange
}: {
  template: WeighingTicketTemplate;
  templates?: WeighingTicketTemplate[];
  record: WeighingRecord;
  onTemplateChange?: (template: WeighingTicketTemplate) => void;
}) {
  const update = (patch: Partial<WeighingTicketTemplate>) => onTemplateChange?.({ ...template, ...patch });
  const ticketLines = normalizeTicketLines(template);
  const [selectedLineId, setSelectedLineId] = useState(ticketLines[0]?.id ?? "");
  const selectedLine = ticketLines.find((line) => line.id === selectedLineId) ?? ticketLines[0];
  const applyTemplate = (nextTemplate: WeighingTicketTemplate) => onTemplateChange?.({ ...nextTemplate, lines: normalizeTicketLines(nextTemplate) });
  const updateLines = (lines: TicketLine[]) => update({ lines, fields: linesToFields(lines) });
  const updateLine = (lineId: string, patch: Partial<TicketLine>) => updateLines(ticketLines.map((line) => line.id === lineId ? { ...line, ...patch } : line));
  const moveLine = (lineId: string, direction: -1 | 1) => {
    const index = ticketLines.findIndex((line) => line.id === lineId);
    const nextIndex = index + direction;
    if (index < 0 || nextIndex < 0 || nextIndex >= ticketLines.length) return;
    const nextLines = [...ticketLines];
    const [line] = nextLines.splice(index, 1);
    nextLines.splice(nextIndex, 0, line);
    updateLines(nextLines);
  };
  const reorderLine = (draggedLineId: string, targetLineId: string) => {
    if (draggedLineId === targetLineId) return;
    const draggedIndex = ticketLines.findIndex((line) => line.id === draggedLineId);
    const targetIndex = ticketLines.findIndex((line) => line.id === targetLineId);
    if (draggedIndex < 0 || targetIndex < 0) return;
    const nextLines = [...ticketLines];
    const [draggedLine] = nextLines.splice(draggedIndex, 1);
    nextLines.splice(targetIndex, 0, draggedLine);
    updateLines(nextLines);
    setSelectedLineId(draggedLineId);
  };
  const addLine = () => {
    const line = createTicketLine("text", "Nouvelle ligne");
    updateLines([...ticketLines, line]);
    setSelectedLineId(line.id);
  };
  const removeLine = (lineId: string) => {
    const nextLines = ticketLines.filter((line) => line.id !== lineId);
    updateLines(nextLines);
    setSelectedLineId(nextLines[0]?.id ?? "");
  };

  return (
    <section className="grid gap-4 rounded-lg border border-border bg-surface p-4 shadow-card xl:grid-cols-[360px_320px] xl:justify-center">
      <div className="min-w-0">
        <div className="flex items-center justify-between gap-3">
          <div>
            <h2 className="text-base font-semibold text-slate-950">Gestion d’impression</h2>
            <p className="text-sm text-slate-500">Ticket de pesée standard, prêt à personnaliser.</p>
          </div>
          <Button variant="secondary" size="icon" aria-label="Aperçu">
            <Eye className="h-4 w-4" />
          </Button>
        </div>
        {templates && templates.length > 0 && (
          <div className="mt-4">
            <p className="text-sm font-semibold text-slate-700">Exemples de tickets</p>
            <div className="mt-2 grid gap-2 sm:grid-cols-2">
              {templates.map((preset) => (
                <button
                  className={`min-h-16 rounded-2xl border px-4 py-3 text-left transition-[transform,border-color,background-color] duration-200 ease-out active:scale-[0.99] ${preset.id === template.id ? "border-brand bg-brand-soft text-blue-800" : "border-border bg-white text-slate-700"}`}
                  key={preset.id}
                  type="button"
                  onClick={() => applyTemplate(preset)}
                >
                  <span className="block text-sm font-bold">{preset.name}</span>
                  <span className="mt-1 block text-xs font-semibold opacity-70">{preset.paperWidth} · {preset.copies} copie{preset.copies > 1 ? "s" : ""}</span>
                </button>
              ))}
            </div>
          </div>
        )}
        <div className="mt-4 grid gap-3 sm:grid-cols-2">
          <Field label="Nom société" value={template.companyName} onChange={(companyName) => update({ companyName })} />
          <Field label="Titre ticket" value={template.title} onChange={(title) => update({ title })} />
          <label className="grid gap-1.5 text-sm font-semibold text-slate-700">
            Largeur papier
            <select className="min-h-11 rounded-2xl border border-border bg-white px-4 text-sm font-medium text-slate-950 outline-none transition focus:border-brand focus:outline focus:outline-[3px] focus:outline-brand/30" value={template.paperWidth} onChange={(event) => update({ paperWidth: event.target.value as "58mm" | "80mm" })}>
              <option value="58mm">58 mm</option>
              <option value="80mm">80 mm</option>
            </select>
          </label>
          <Field label="Copies" value={String(template.copies)} onChange={(copies) => update({ copies: Math.max(1, Number(copies) || 1) })} />
          <Toggle label="Logo société" checked={template.logoEnabled} onChange={(logoEnabled) => update({ logoEnabled })} />
          <Toggle label="QR code" checked={template.qrCodeEnabled} onChange={(qrCodeEnabled) => update({ qrCodeEnabled })} />
          <label className="grid gap-1.5 text-sm font-semibold text-slate-700 sm:col-span-2">
            Pied de ticket
            <textarea className="min-h-20 rounded-2xl border border-border bg-white px-4 py-3 text-sm font-medium text-slate-950 outline-none transition focus:border-brand focus:outline focus:outline-[3px] focus:outline-brand/30" value={template.footer} onChange={(event) => update({ footer: event.target.value })} />
          </label>
        </div>
        <div className="mt-5 rounded-2xl border border-border bg-surface-soft p-3">
          <div className="flex items-center justify-between gap-3">
            <div>
              <p className="text-sm font-semibold text-slate-700">Ligne sélectionnée</p>
              <p className="text-xs font-medium text-slate-500">Cliquez directement une ligne dans le ticket.</p>
            </div>
            <Button variant="secondary" size="sm" onClick={addLine}>
              <Plus className="h-4 w-4" />
              Ajouter
            </Button>
          </div>
          {selectedLine ? (
            <div className="mt-3 grid gap-2">
              <input className="min-h-10 rounded-xl border border-border bg-white px-3 text-sm font-semibold outline-none focus:border-brand" value={selectedLine.label} onChange={(event) => updateLine(selectedLine.id, { label: event.target.value })} />
              <select className="min-h-10 rounded-xl border border-border bg-white px-3 text-sm font-semibold outline-none focus:border-brand" value={selectedLine.source} onChange={(event) => updateLine(selectedLine.id, { source: event.target.value as TicketLineSource })}>
                    {ticketLineSources.map((source) => <option key={source.value} value={source.value}>{source.label}</option>)}
              </select>
              {selectedLine.source === "text" && (
                <input className="min-h-10 rounded-xl border border-border bg-white px-3 text-sm font-medium outline-none focus:border-brand" placeholder="Texte à imprimer" value={selectedLine.text ?? ""} onChange={(event) => updateLine(selectedLine.id, { text: event.target.value })} />
              )}
              {selectedLine.source === "command" && (
                <input className="min-h-10 rounded-xl border border-border bg-white px-3 text-sm font-medium outline-none focus:border-brand" placeholder="?ID, ?SN, ?TN..." value={selectedLine.command ?? "?SN"} onChange={(event) => updateLine(selectedLine.id, { command: event.target.value })} />
              )}
              <div className="grid grid-cols-[1fr_auto] gap-2">
                <select className="min-h-10 rounded-xl border border-border bg-white px-3 text-sm font-semibold outline-none focus:border-brand" value={selectedLine.fontSize} onChange={(event) => updateLine(selectedLine.id, { fontSize: event.target.value as TicketLine["fontSize"] })}>
                  <option value="small">Petit</option>
                  <option value="normal">Normal</option>
                  <option value="large">Grand</option>
                </select>
                <label className="flex min-h-10 items-center gap-2 rounded-xl border border-border bg-white px-3 text-sm font-bold">
                  <input className="h-4 w-4 accent-blue-600" checked={selectedLine.bold} type="checkbox" onChange={(event) => updateLine(selectedLine.id, { bold: event.target.checked })} />
                  Gras
                </label>
              </div>
              <div className="flex gap-1">
                <Button variant="secondary" size="icon" aria-label="Monter" onClick={() => moveLine(selectedLine.id, -1)}><ArrowUp className="h-4 w-4" /></Button>
                <Button variant="secondary" size="icon" aria-label="Descendre" onClick={() => moveLine(selectedLine.id, 1)}><ArrowDown className="h-4 w-4" /></Button>
                <Button variant="secondary" size="icon" aria-label="Supprimer" onClick={() => removeLine(selectedLine.id)}><Trash2 className="h-4 w-4" /></Button>
              </div>
            </div>
          ) : null}
        </div>
        <div className="mt-4 flex flex-wrap gap-2">
          <Button>
            <Save className="h-4 w-4" />
            Enregistrer
          </Button>
          <Button variant="secondary">
            <Printer className="h-4 w-4" />
            Tester impression
          </Button>
          <Button variant="secondary">
            <Copy className="h-4 w-4" />
            Dupliquer
          </Button>
        </div>
      </div>
      <TicketPreview
        editable
        lines={ticketLines}
        onAddLine={addLine}
        onSelectLine={setSelectedLineId}
        onReorderLine={reorderLine}
        onUpdateLine={updateLine}
        selectedLineId={selectedLine?.id}
        template={template}
        record={record}
      />
    </section>
  );
}

export function TicketPreview({
  editable = false,
  lines,
  onAddLine,
  onReorderLine,
  onSelectLine,
  onUpdateLine,
  selectedLineId,
  template,
  record
}: {
  editable?: boolean;
  lines?: TicketLine[];
  onAddLine?: () => void;
  onReorderLine?: (draggedLineId: string, targetLineId: string) => void;
  onSelectLine?: (lineId: string) => void;
  onUpdateLine?: (lineId: string, patch: Partial<TicketLine>) => void;
  selectedLineId?: string;
  template: WeighingTicketTemplate;
  record: WeighingRecord;
}) {
  const visibleLines = (lines ?? normalizeTicketLines(template)).filter((line) => line.enabled && !["logo", "companyName", "ticketTitle", "qrCode"].includes(line.source));
  const [qrDataUrl, setQrDataUrl] = useState("");

  useEffect(() => {
    let active = true;
    if (!template.qrCodeEnabled) {
      setQrDataUrl("");
      return;
    }
    QRCode.toDataURL(buildTicketQrPayload(record), {
      errorCorrectionLevel: "M",
      margin: 1,
      scale: 4
    }).then((url) => {
      if (active) setQrDataUrl(url);
    }).catch(() => {
      if (active) setQrDataUrl("");
    });
    return () => {
      active = false;
    };
  }, [record, template.qrCodeEnabled]);

  return (
    <div className="mx-auto w-full max-w-[280px] rounded-lg bg-slate-100 p-3">
      <div className="rounded-sm bg-white px-4 py-5 font-mono text-[12px] leading-5 text-slate-950 shadow-card">
        {template.logoEnabled && <p className="text-center text-[10px]">[LOGO]</p>}
        {(template.fields.includes("companyName") || template.logoEnabled) && <p className="text-center font-bold">{template.companyName}</p>}
        {template.fields.includes("ticketTitle") || template.title ? <p className="text-center">{template.title}</p> : null}
        <Divider />
        {visibleLines.map((line) => (
          <TicketFieldLine editable={editable} isSelected={line.id === selectedLineId} line={line} key={line.id} onReorderLine={onReorderLine} onSelectLine={onSelectLine} onUpdateLine={onUpdateLine} record={record} />
        ))}
        {editable && (
          <button className="mt-2 w-full rounded border border-dashed border-slate-300 py-1 text-center text-[11px] font-bold text-slate-500" type="button" onClick={onAddLine}>
            + Ajouter une ligne
          </button>
        )}
        <Divider />
        {template.qrCodeEnabled && (
          <div className="mx-auto my-3 grid h-[74px] w-[74px] place-items-center bg-white">
            {qrDataUrl ? <img className="h-[74px] w-[74px]" src={qrDataUrl} alt="QR code LabConnect" /> : <QrCode className="h-10 w-10" />}
          </div>
        )}
        <p className="text-center text-[11px]">{template.footer}</p>
      </div>
    </div>
  );
}

function TicketFieldLine({
  editable,
  isSelected,
  line,
  onSelectLine,
  onReorderLine,
  onUpdateLine,
  record
}: {
  editable?: boolean;
  isSelected?: boolean;
  line: TicketLine;
  onReorderLine?: (draggedLineId: string, targetLineId: string) => void;
  onSelectLine?: (lineId: string) => void;
  onUpdateLine?: (lineId: string, patch: Partial<TicketLine>) => void;
  record: WeighingRecord;
}) {
  if (line.source === "signature") {
    return <EditableLineFrame editable={editable} isSelected={isSelected} line={line} onReorderLine={onReorderLine} onSelectLine={onSelectLine}><SignatureBlock label={line.label} /></EditableLineFrame>;
  }
  if (line.source === "blank") {
    return <EditableLineFrame editable={editable} isSelected={isSelected} line={line} onReorderLine={onReorderLine} onSelectLine={onSelectLine}><div className="h-4" /></EditableLineFrame>;
  }
  const ticketLine = getTicketFieldLine(line, record);
  if (!ticketLine) return null;
  return (
    <EditableLineFrame editable={editable} isSelected={isSelected} line={line} onReorderLine={onReorderLine} onSelectLine={onSelectLine}>
      <Line
        editable={editable}
        fontSize={line.fontSize}
        label={ticketLine.label}
        onLabelChange={(label) => onUpdateLine?.(line.id, { label })}
        onTextChange={(text) => onUpdateLine?.(line.id, { text })}
        source={line.source}
        strong={line.bold || ticketLine.strong}
        value={ticketLine.value}
      />
    </EditableLineFrame>
  );
}

function EditableLineFrame({
  children,
  editable,
  isSelected,
  line,
  onReorderLine,
  onSelectLine
}: {
  children: ReactNode;
  editable?: boolean;
  isSelected?: boolean;
  line: TicketLine;
  onReorderLine?: (draggedLineId: string, targetLineId: string) => void;
  onSelectLine?: (lineId: string) => void;
}) {
  if (!editable) return <>{children}</>;
  return (
    <div
      className={`cursor-grab rounded px-1 py-0.5 transition active:cursor-grabbing ${isSelected ? "bg-blue-50 outline outline-1 outline-blue-500" : "hover:bg-slate-50"}`}
      draggable
      onClick={() => onSelectLine?.(line.id)}
      onDragOver={(event) => event.preventDefault()}
      onDragStart={(event) => event.dataTransfer.setData("text/plain", line.id)}
      onDrop={(event) => {
        event.preventDefault();
        const draggedLineId = event.dataTransfer.getData("text/plain");
        onReorderLine?.(draggedLineId, line.id);
      }}
    >
      {children}
    </div>
  );
}

function SignatureBlock({ label }: { label: string }) {
  return (
    <div className="mt-2">
      <p>{label || "Signature"}</p>
      <div className="h-16" />
    </div>
  );
}

function getTicketFieldLine(line: TicketLine, record: WeighingRecord) {
  const field = line.source;
  if (field === "text") {
    return { label: line.label, value: line.text ?? "", strong: line.bold };
  }
  if (field === "command") {
    const command = normalizeCommand(line.command);
    return { label: line.label || command, value: record.commandResponses?.[command] ?? "En attente", strong: line.bold };
  }
  if (field === "blank") return null;
  const lines: Partial<Record<TicketField, { label: string; value: string; strong?: boolean }>> = {
    dateTime: { label: "Date", value: record.dateTime },
    operator: { label: "Opérateur", value: record.operator },
    balanceName: { label: "Balance", value: record.balanceName },
    rawBalanceLine: { label: "Ligne brute", value: record.rawBalanceLine },
    weight: { label: "Poids", value: record.weight, strong: true },
    grossWeight: { label: "Brut", value: record.grossWeight ?? record.weight },
    tareWeight: { label: "Tare", value: record.tareWeight ?? "-" },
    netWeight: { label: "Net", value: record.netWeight ?? record.weight, strong: true },
    unit: { label: "Unité", value: record.unit },
    lotNumber: { label: "Lot", value: record.lotNumber },
    sampleId: { label: "Échantillon", value: record.sampleId },
    methodName: { label: "Méthode", value: record.methodName ?? "-" },
    dryingTemperature: { label: "Température", value: record.dryingTemperature ?? "-" },
    dryingTime: { label: "Durée", value: record.dryingTime ?? "-" },
    startWeight: { label: "Poids initial", value: record.startWeight ?? "-" },
    dryWeight: { label: "Poids sec", value: record.dryWeight ?? "-" },
    moistureContent: { label: "Humidité", value: record.moistureContent ?? "-", strong: true },
    resultStatus: { label: "Résultat", value: record.resultStatus ?? "-" },
    signature: { label: "Signature", value: record.signature ?? "À signer" }
  };
  const ticketLine = lines[field] ?? null;
  return ticketLine ? { ...ticketLine, label: line.label || ticketLine.label } : null;
}

function Field({ label, value, onChange }: { label: string; value: string; onChange: (value: string) => void }) {
  return (
    <label className="grid gap-1.5 text-sm font-semibold text-slate-700">
      {label}
      <input className="min-h-11 rounded-2xl border border-border bg-white px-4 text-sm font-medium text-slate-950 outline-none transition focus:border-brand focus:outline focus:outline-[3px] focus:outline-brand/30" value={value} onChange={(event) => onChange(event.target.value)} />
    </label>
  );
}

function Toggle({ label, checked, onChange }: { label: string; checked: boolean; onChange: (value: boolean) => void }) {
  return (
    <label className="flex min-h-11 items-center justify-between gap-3 rounded-2xl border border-border bg-white px-4 text-sm font-semibold text-slate-700">
      {label}
      <input className="h-5 w-5 accent-blue-600" type="checkbox" checked={checked} onChange={(event) => onChange(event.target.checked)} />
    </label>
  );
}

function Divider() {
  return <div className="my-2 border-t border-dashed border-slate-300" />;
}

function Line({
  editable,
  fontSize = "normal",
  label,
  onLabelChange,
  onTextChange,
  source,
  value,
  strong
}: {
  editable?: boolean;
  fontSize?: TicketLine["fontSize"];
  label: string;
  onLabelChange?: (label: string) => void;
  onTextChange?: (text: string) => void;
  source: TicketLineSource;
  value: string;
  strong?: boolean;
}) {
  const sizeClass = fontSize === "large" ? "text-sm" : fontSize === "small" ? "text-[11px]" : "";
  if (editable && source === "text") {
    return (
      <input className={`w-full bg-transparent outline-none ${sizeClass} ${strong ? "font-bold" : ""}`} value={value} onChange={(event) => onTextChange?.(event.target.value)} onClick={(event) => event.stopPropagation()} />
    );
  }
  return (
    <div className={`flex justify-between gap-3 ${sizeClass} ${strong ? "font-bold" : ""}`}>
      {editable ? (
        <input className="min-w-0 flex-1 bg-transparent outline-none" value={label} onChange={(event) => onLabelChange?.(event.target.value)} onClick={(event) => event.stopPropagation()} />
      ) : <span>{label}</span>}
      <span className="text-right">{value}</span>
    </div>
  );
}

const ticketLineSources: Array<{ label: string; value: TicketLineSource }> = [
  { label: "Texte libre", value: "text" },
  { label: "Ligne vide", value: "blank" },
  { label: "Commande balance", value: "command" },
  { label: "Date", value: "dateTime" },
  { label: "Opérateur", value: "operator" },
  { label: "Balance", value: "balanceName" },
  { label: "Réponse commande / ligne brute", value: "rawBalanceLine" },
  { label: "Poids", value: "weight" },
  { label: "Brut", value: "grossWeight" },
  { label: "Tare", value: "tareWeight" },
  { label: "Net", value: "netWeight" },
  { label: "Lot", value: "lotNumber" },
  { label: "Échantillon", value: "sampleId" },
  { label: "Méthode", value: "methodName" },
  { label: "Température", value: "dryingTemperature" },
  { label: "Durée", value: "dryingTime" },
  { label: "Poids initial", value: "startWeight" },
  { label: "Poids sec", value: "dryWeight" },
  { label: "Humidité", value: "moistureContent" },
  { label: "Résultat", value: "resultStatus" },
  { label: "Signature", value: "signature" },
  { label: "QR code", value: "qrCode" }
];

function normalizeTicketLines(template: WeighingTicketTemplate): TicketLine[] {
  if (template.lines && template.lines.length > 0) return template.lines;
  return template.fields
    .filter((field) => !["logo", "companyName", "ticketTitle"].includes(field))
    .map((field) => createTicketLine(field, defaultLabelForSource(field)));
}

function createTicketLine(source: TicketLineSource, label: string): TicketLine {
  return {
    id: `line-${source}-${Math.random().toString(36).slice(2, 9)}`,
    label,
    source,
    command: source === "command" ? "?SN" : undefined,
    text: source === "text" ? label : undefined,
    enabled: true,
    bold: source === "weight" || source === "netWeight" || source === "moistureContent",
    fontSize: source === "weight" || source === "netWeight" || source === "moistureContent" ? "large" : "normal",
    valueAlign: "right"
  };
}

function linesToFields(lines: TicketLine[]) {
  return lines
    .map((line) => line.source)
    .filter((source): source is TicketField => !["text", "blank", "command"].includes(source));
}

function defaultLabelForSource(source: TicketLineSource) {
  if (source === "rawBalanceLine") return "Ligne brute";
  if (source === "command") return "Commande";
  return ticketLineSources.find((item) => item.value === source)?.label ?? "Ligne";
}

function normalizeCommand(command: string | undefined) {
  const nextCommand = (command || "?SN").trim().toUpperCase();
  return nextCommand.startsWith("?") ? nextCommand : `?${nextCommand}`;
}
