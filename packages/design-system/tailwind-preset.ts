const preset = {
  theme: {
    extend: {
      fontFamily: {
        sans: ["Geist", "Inter", "SF Pro Display", "Segoe UI", "Arial", "sans-serif"]
      },
      colors: {
        background: "var(--background)",
        surface: "var(--surface)",
        "surface-soft": "var(--surface-soft)",
        "surface-hover": "var(--surface-hover)",
        border: "var(--border)",
        "border-strong": "var(--border-strong)",
        brand: "var(--brand)",
        "brand-hover": "var(--brand-hover)",
        "brand-soft": "var(--brand-soft)",
        success: "var(--success)",
        "success-soft": "var(--success-soft)",
        warning: "var(--warning)",
        "warning-soft": "var(--warning-soft)",
        danger: "var(--danger)",
        "danger-soft": "var(--danger-soft)",
        info: "var(--info)",
        "info-soft": "var(--info-soft)"
      },
      boxShadow: {
        card: "0 16px 48px rgba(15, 23, 42, 0.08)",
        sheet: "0 -24px 80px rgba(15, 23, 42, 0.20)",
        float: "0 24px 80px rgba(15, 23, 42, 0.12)"
      },
      transitionTimingFunction: {
        lab: "cubic-bezier(0.2, 0.8, 0.2, 1)"
      },
      transitionDuration: {
        120: "120ms",
        180: "180ms",
        280: "280ms"
      },
      keyframes: {
        "bluetooth-pulse": {
          "0%": { transform: "scale(0.82)", opacity: "0.56" },
          "70%": { transform: "scale(1.28)", opacity: "0" },
          "100%": { transform: "scale(1.28)", opacity: "0" }
        },
        "sheet-in": {
          "0%": { transform: "translateY(24px)", opacity: "0" },
          "100%": { transform: "translateY(0)", opacity: "1" }
        }
      },
      animation: {
        "bluetooth-pulse": "bluetooth-pulse 1800ms ease-out infinite",
        "sheet-in": "sheet-in 280ms cubic-bezier(0.2, 0.8, 0.2, 1)"
      }
    }
  }
};

export default preset;
