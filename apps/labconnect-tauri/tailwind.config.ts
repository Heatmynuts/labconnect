import type { Config } from "tailwindcss";
import labConnectPreset from "@labconnect/design-system/tailwind-preset";

const config = {
  presets: [labConnectPreset],
  content: [
    "./index.html",
    "./src/**/*.{ts,tsx}",
    "../../packages/ui/src/**/*.{ts,tsx}"
  ],
  theme: {
    extend: {}
  },
  plugins: []
} satisfies Config;

export default config;
