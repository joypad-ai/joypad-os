// Local Moondream / Ollama client. POSTs a JPEG (base64) + prompt to Ollama's
// /api/generate endpoint and returns the raw text response.
//
// Why Ollama: keeps the joypad-mcp TS-only (no Python bridge). Moondream3 with
// the photon update runs Mac-native at ~1s end-to-end, which means:
//   - Image bytes never leave the laptop (no upload).
//   - The cloud-LLM turn (me) becomes text-only — no multimodal prefill cost.
//   - "few lines of context + screenshot" pattern: structured JSON is tiny.
//
// Setup:
//   brew install ollama
//   ollama serve &
//   ollama pull moondream    # ~2GB, one-time download

// MOONDREAM_URL points at the actual Moondream3-Photon Python server
// (python/moondream_server.py). Falls back to Ollama for older Moondream2
// or any other vision model already pulled.
const MOONDREAM_URL = process.env.MOONDREAM_URL;
const DEFAULT_HOST = process.env.OLLAMA_HOST ?? "http://localhost:11434";

export interface MoondreamRequest {
  jpeg: Buffer;
  prompt: string;
  model?: string;        // default "moondream"
  host?: string;         // default $OLLAMA_HOST or http://localhost:11434
  timeoutMs?: number;    // default 90000
}

export interface MoondreamResponse {
  text: string;
  model: string;
  ms: number;            // wall-clock latency
  prompt_eval_count?: number;
  eval_count?: number;
}

export async function analyzeImage(req: MoondreamRequest): Promise<MoondreamResponse> {
  // If MOONDREAM_URL is set, use the dedicated Python server (Moondream3 Photon)
  // unmodified. Otherwise fall back to Ollama-compatible endpoint.
  const host = req.host ?? MOONDREAM_URL ?? DEFAULT_HOST;
  const model = req.model ?? "moondream";
  const url = MOONDREAM_URL
    ? MOONDREAM_URL
    : `${host.replace(/\/$/, "")}/api/generate`;
  const body = JSON.stringify({
    model,
    prompt: req.prompt,
    images: [req.jpeg.toString("base64")],
    stream: false,
  });
  const t0 = Date.now();
  const ctrl = new AbortController();
  const timer = setTimeout(() => ctrl.abort(), req.timeoutMs ?? 90000);
  let resp: Response;
  try {
    resp = await fetch(url, {
      method: "POST",
      headers: { "content-type": "application/json" },
      body,
      signal: ctrl.signal,
    });
  } catch (e: any) {
    if (e?.name === "AbortError") {
      throw new Error(`Moondream timed out after ${req.timeoutMs ?? 30000}ms`);
    }
    if (e?.cause?.code === "ECONNREFUSED" || /ECONNREFUSED|fetch failed/i.test(String(e))) {
      throw new Error(`Cannot reach Ollama at ${host} — is \`ollama serve\` running and \`ollama pull ${model}\` done?`);
    }
    throw e;
  } finally {
    clearTimeout(timer);
  }
  if (!resp.ok) {
    const detail = await resp.text().catch(() => "");
    throw new Error(`Ollama ${resp.status}: ${detail.slice(0, 300)}`);
  }
  const data = (await resp.json()) as { response?: string; prompt_eval_count?: number; eval_count?: number };
  return {
    text: (data.response ?? "").trim(),
    model,
    ms: Date.now() - t0,
    prompt_eval_count: data.prompt_eval_count,
    eval_count: data.eval_count,
  };
}

// Built-in prompts for common games. Worth a few iterations to keep them
// concise — Moondream prefers short, direct asks.
export const GAME_PROMPTS: Record<string, string> = {
  tetris: [
    "You are looking at a Tetris game screen.",
    "Reply with ONLY a JSON object (no markdown, no other text) with these fields:",
    '  "piece": current falling tetromino, one of "I","O","T","S","Z","J","L", or null',
    '  "next": next piece in the NEXT preview, same set or null',
    '  "hold": piece in the HOLD slot or null',
    '  "score": integer score shown',
    '  "level": integer level',
    '  "lines_to_clear": integer if shown, else null',
    '  "pile_summary": one short sentence describing the existing stack and any obvious gaps',
    "If the screen is a menu or game-over rather than gameplay, set all fields to null and put a description in pile_summary.",
  ].join("\n"),

  generic: "Describe what is on this screen in one short paragraph. Note any text, menus, prompts, or game state.",
};
