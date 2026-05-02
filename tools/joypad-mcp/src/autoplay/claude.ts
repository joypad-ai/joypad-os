// Claude Haiku as the slow-path reasoner. One API call per decision —
// vision + reasoning in a single round trip, replacing the moondream+qwen
// chain. Haiku 4.5 returns ~700ms-1s end-to-end with structured JSON.
//
// Set ANTHROPIC_API_KEY in env (or in joypad-mcp's mcpServers.env block).
// Model selectable via ANTHROPIC_MODEL — defaults to claude-haiku-4-5-20251001.
//
// Token cost reference (Haiku 4.5 list price): ~$1/MTok in, ~$5/MTok out.
// Each call ≈ 1k image tokens + 200 text tokens + 100 output tokens ≈ $0.001.
// At 30 decisions/min that's ~$0.03/min — cheap enough to leave running.

import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";
import { ScreenMode, QwenAction } from "./qwen.js";

const API_KEY = process.env.ANTHROPIC_API_KEY;
const MODEL = process.env.ANTHROPIC_MODEL ?? "claude-haiku-4-5-20251001";

// "Soul" — the personality + domain expertise that makes Claude actually play
// Tetris instead of just picking the lowest column. Loaded once at module
// import. See tetris_soul.md for the prompt itself.
const SOUL_PATH = join(dirname(fileURLToPath(import.meta.url)), "tetris_soul.md");

export interface ClaudeInput {
  jpeg: Buffer;
  game: string;
  heights: number[];
  last_action?: QwenAction;
  ticks_since_change?: number;
}

export interface ClaudeResult {
  action: QwenAction;
  ms: number;
  input_tokens?: number;
  output_tokens?: number;
  cost_usd: number;
}

// Haiku 4.5 list pricing as of writing. If the model env var changes, these
// stay accurate enough for budget guard purposes (everyone in the Haiku tier
// is similarly cheap).
const HAIKU_INPUT_USD_PER_TOKEN = 1.0 / 1_000_000;
const HAIKU_OUTPUT_USD_PER_TOKEN = 5.0 / 1_000_000;

const SYSTEM = readFileSync(SOUL_PATH, "utf-8");

export async function decideWithClaude(input: ClaudeInput): Promise<ClaudeResult> {
  if (!API_KEY) throw new Error("ANTHROPIC_API_KEY not set");
  const t0 = Date.now();
  const userText = `Pile heights (col 0..9): ${JSON.stringify(input.heights)}.
Last action: ${input.last_action ? JSON.stringify(input.last_action) : "none"}.
Ticks since heights last changed: ${input.ticks_since_change ?? 0}.

What is the next action?`;

  const body = JSON.stringify({
    model: MODEL,
    max_tokens: 200,
    system: SYSTEM,
    messages: [
      {
        role: "user",
        content: [
          {
            type: "image",
            source: { type: "base64", media_type: "image/jpeg", data: input.jpeg.toString("base64") },
          },
          { type: "text", text: userText },
        ],
      },
    ],
  });

  const ctrl = new AbortController();
  const timer = setTimeout(() => ctrl.abort(), 15_000);
  let resp: Response;
  try {
    resp = await fetch("https://api.anthropic.com/v1/messages", {
      method: "POST",
      headers: {
        "x-api-key": API_KEY,
        "anthropic-version": "2023-06-01",
        "content-type": "application/json",
      },
      body,
      signal: ctrl.signal,
    });
  } finally {
    clearTimeout(timer);
  }
  if (!resp.ok) {
    const txt = await resp.text().catch(() => "");
    throw new Error(`Anthropic ${resp.status}: ${txt.slice(0, 300)}`);
  }
  const data = (await resp.json()) as {
    content: Array<{ type: string; text?: string }>;
    usage?: { input_tokens?: number; output_tokens?: number };
  };
  const text = data.content?.find((c) => c.type === "text")?.text ?? "";
  const inT = data.usage?.input_tokens ?? 0;
  const outT = data.usage?.output_tokens ?? 0;
  const cost = inT * HAIKU_INPUT_USD_PER_TOKEN + outT * HAIKU_OUTPUT_USD_PER_TOKEN;
  return {
    action: parseAction(text),
    ms: Date.now() - t0,
    input_tokens: inT,
    output_tokens: outT,
    cost_usd: cost,
  };
}

function parseAction(raw: string): QwenAction {
  let txt = raw.trim();
  const fence = txt.match(/```(?:json)?\s*([\s\S]*?)```/);
  if (fence) txt = fence[1].trim();
  try {
    const obj = JSON.parse(txt);
    if (obj && typeof obj === "object" && obj.action) return obj as QwenAction;
  } catch {}
  return { action: "wait", reason: `unparseable: ${raw.slice(0, 80)}` };
}
