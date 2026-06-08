import type { LucideIcon } from "lucide-react";
import type * as React from "react";
import { Button } from "./button";
import { cn } from "../lib/cn";

export function ActionButton({
  icon: Icon,
  label,
  active,
  className,
  ...props
}: {
  icon: LucideIcon;
  label: string;
  active?: boolean;
  className?: string;
} & React.ButtonHTMLAttributes<HTMLButtonElement>) {
  return (
    <Button
      variant={active ? "primary" : "secondary"}
      className={cn("min-h-[52px] flex-1 rounded-2xl px-3", className)}
      {...props}
    >
      <Icon aria-hidden className="h-4 w-4" />
      <span>{label}</span>
    </Button>
  );
}
