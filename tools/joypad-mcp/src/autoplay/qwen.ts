// Qwen reasoning layer — text-only LLM via Ollama. Takes structured screen
// state (moondream3 caption + matrix parser output) and decides the next
// action. This is the "Q" of the whisper+qwen+moondream stack from the tweet.
//
// We use the text model (qwen2.5:7b) rather than the vision variant —
// moondream3 already did the perception, qwen just reasons over text.

const OLLAMA = process.env.OLLAMA_HOST ?? "http://localhost:11434";

export type ScreenMode = "playing" | "menu" | "game_over" | "paused" | "unknown";

export interface QwenAction {
  action: "press_button" | "place_piece" | "wait";
  buttons?: string;          // for press_button: "B1", "DU", "S2+S1"
  duration_ms?: number;
  target_col?: number;       // for place_piece: 0–9
  rotation?: number;         // for place_piece: 0,1,2,3 (90° steps CW)
  screen_mode?: ScreenMode;  // qwen's classification of what screen we're on
  reason?: string;
}

export interface QwenInput {
  game: string;
  caption: string;            // from moondream3
  heights: number[];          // from parser
  // Optional context about previous tick so qwen can detect "stuck" states
  last_action?: QwenAction;
  last_action_outcome?: "advanced" | "no_change" | "error";
  ticks_since_change?: number;
}

const SYSTEM_PROMPT = `You are controlling Tetris Ultimate on Xbox.
Each turn you receive a screen description and pile heights. First classify
the screen, then choose the action that fits THAT screen — do NOT default to
place_piece if the screen is not active gameplay.

Output JSON (no markdown). Required field: screen_mode.

Possible screen_mode values:
  "playing"    — gameplay is active, a piece is falling and the matrix shows the playfield
  "menu"       — pre-game menu (e.g. "PRESS A TO SELECT A MATRIX", main menu)
  "game_over"  — results / RESULTS / GAME OVER / RETRY shown
  "paused"     — PAUSED overlay visible
  "unknown"    — anything else / loading / cutscene

Decision rules — read CAREFULLY, button mapping differs per screen:
  if screen_mode == "menu"      → {"action":"press_button","buttons":"B1"}     (A confirms)
  if screen_mode == "game_over" → {"action":"press_button","buttons":"B3"}     (X = RETRY on results)
  if screen_mode == "paused"    → {"action":"press_button","buttons":"B1"}     (A = RESUME)
  if screen_mode == "playing"   → {"action":"place_piece","target_col":N,"rotation":0}
                                   (N = column 0..9 with the lowest height)
  if screen_mode == "unknown"   → {"action":"press_button","buttons":"B1"}     (try A, retry next tick)

Important: On the GAME OVER / RESULTS screen, the A button does NOTHING.
You MUST use X (mapped to "B3") to RETRY, or B (mapped to "B2") for main menu.
Default to B3 (RETRY) for game_over.

NEVER pick place_piece on a results / game-over / menu screen, even if the
description mentions a piece falling in the background animation.

Examples (output exactly like these):
  {"screen_mode":"menu","action":"press_button","buttons":"B1","reason":"PRESS A to select"}
  {"screen_mode":"game_over","action":"press_button","buttons":"B1","reason":"results screen, restart"}
  {"screen_mode":"playing","action":"place_piece","target_col":2,"rotation":0,"reason":"col 2 lowest"}

Buttons: B1=A, B2=B, B3=X, B4=Y, S2=Menu, S1=View, A1=Guide, DU/DD/DL/DR=Dpad.`;

export async function decideNext(input: QwenInput, model = "qwen2.5:7b"): Promise<QwenAction> {
  const userMsg = JSON.stringify({
    game: input.game,
    screen_description: input.caption,
    pile_heights: input.heights,
    last_action: input.last_action,
    last_action_outcome: input.last_action_outcome,
    ticks_since_change: input.ticks_since_change,
  });

  const body = JSON.stringify({
    model,
    prompt: userMsg,
    system: SYSTEM_PROMPT,
    format: "json",
    stream: false,
    options: { temperature: 0.1 },
  });

  const ctrl = new AbortController();
  const timer = setTimeout(() => ctrl.abort(), 30_000);
  try {
    const resp = await fetch(`${OLLAMA}/api/generate`, {
      method: "POST",
      headers: { "content-type": "application/json" },
      body,
      signal: ctrl.signal,
    });
    if (!resp.ok) throw new Error(`ollama ${resp.status}: ${await resp.text().catch(() => "")}`);
    const data = (await resp.json()) as { response?: string };
    const raw = (data.response ?? "").trim();
    return parseAction(raw);
  } finally {
    clearTimeout(timer);
  }
}

function parseAction(raw: string): QwenAction {
  // Be lenient — strip markdown code fences if qwen wraps output.
  let txt = raw;
  const fence = txt.match(/```(?:json)?\s*([\s\S]*?)```/);
  if (fence) txt = fence[1].trim();
  try {
    const obj = JSON.parse(txt);
    if (obj && typeof obj === "object" && obj.action) return obj as QwenAction;
  } catch {}
  // Last resort: assume "wait" if we can't parse — better than spamming.
  return { action: "wait", reason: `unparseable: ${raw.slice(0, 80)}` };
}
