// Tetris autoplay loop with the full whisper+qwen+moondream stack
// (minus whisper — voice not used here):
//
//   webcam frame ──► moondream3 (caption) ──┐
//                  └► matrix parser (heights)─┴► qwen2.5 (decide) ──► action
//
// The cloud LLM is NOT in this loop. Each tick is fully local — no Anthropic
// round trip. Approximate budget per tick: moondream ~1s + qwen ~500ms +
// parser ~10ms + combo execution ~500ms = ~2s per decision.

import { streamingCamera } from "../capture/streaming_camera.js";
import { state as joypadState } from "../state.js";
import { encodeInputEvent, INPUT_TYPE_GAMEPAD, NEUTRAL_ANALOG, PktType } from "../protocol.js";
import { parseButtons } from "../buttons.js";
import { parseMatrix, MatrixCalibration } from "../games/tetris/parser.js";
import { analyzeImage } from "../capture/moondream.js";
import { decideNext, QwenAction, ScreenMode } from "./qwen.js";
import { decideWithClaude } from "./claude.js";

const USE_CLAUDE = !!process.env.ANTHROPIC_API_KEY;

export interface AutoplayHandle {
  stop: () => void;
  status: () => AutoplayState;
}

export type Phase = "idle" | "capture" | "parse" | "moondream" | "qwen" | "execute" | "wait";

export interface TickRecord {
  ts: number;
  path: "slow" | "fast" | "repeat" | "wait";
  total_ms: number;
  capture_ms?: number;
  parse_ms?: number;
  moondream_ms?: number;
  qwen_ms?: number;
  execute_ms?: number;
  screen_mode: ScreenMode;
  caption?: string;
  heights?: number[];
  action?: QwenAction;
  error?: string;
}

export interface AutoplayState {
  running: boolean;
  pieces_placed: number;
  ticks: number;
  last_tick_ms: number;
  last_action: QwenAction | null;
  last_caption: string | null;
  last_heights: number[] | null;
  last_error: string | null;
  started_at: number | null;
  screen_mode: ScreenMode;
  fast_path_streak: number;
  phase: Phase;
  phase_started_at: number;
  recent_ticks: TickRecord[];   // ring buffer, newest last, max ~20
  moondream_calls: number;
  qwen_calls: number;
  last_moondream_at: number;
  claude_calls: number;
  claude_input_tokens: number;
  claude_output_tokens: number;
  claude_spend_usd: number;
  claude_budget_usd: number;
  budget_hit: boolean;
}

const TICK_MS = 400;     // poll fast — inFlight guard prevents reentry
const PIECE_SPAWN_COL = 4;
const MOVE_DURATION_MS = 50;
const MOVE_GAP_MS = 30;

async function applySlot(buttons: number): Promise<void> {
  const conn = joypadState.requireConn();
  await conn.send(
    PktType.INPUT_EVENT,
    encodeInputEvent({
      playerIndex: 0,
      deviceType: INPUT_TYPE_GAMEPAD,
      buttons,
      analog: [...NEUTRAL_ANALOG] as [number, number, number, number, number, number],
      deltaX: 0,
      deltaY: 0,
    }),
  );
  joypadState.lastCommandAt = Date.now();
}

const sleep = (ms: number) => new Promise<void>((r) => setTimeout(r, ms));

async function tap(label: string, durationMs = MOVE_DURATION_MS): Promise<void> {
  const mask = parseButtons(label);
  await applySlot(mask);
  await sleep(durationMs);
  await applySlot(0);
  await sleep(MOVE_GAP_MS);
}

async function executeAction(action: QwenAction): Promise<void> {
  switch (action.action) {
    case "press_button":
      if (!action.buttons) return;
      await tap(action.buttons, action.duration_ms ?? 100);
      return;
    case "place_piece": {
      const target = clamp(action.target_col ?? 4, 0, 9);
      const rotations = clamp(action.rotation ?? 0, 0, 3);
      for (let i = 0; i < rotations; i++) await tap("B1", 60);   // rotate CW
      const delta = target - PIECE_SPAWN_COL;
      const dir = delta < 0 ? "DL" : "DR";
      for (let i = 0; i < Math.abs(delta); i++) await tap(dir);
      await tap("DU", 80);
      return;
    }
    case "wait":
      return;
  }
}

function clamp(n: number, lo: number, hi: number): number {
  return Math.min(hi, Math.max(lo, n));
}

const MOONDREAM_PROMPT = `Describe what is on this Tetris game screen in
ONE short sentence. Note any prominent on-screen text (PRESS A, RESULTS,
PAUSED, GAME OVER, RESTART, etc.) and whether a Tetris piece appears to be
falling in the matrix.`;

export function startTetrisLoop(cal: MatrixCalibration | undefined): AutoplayHandle {
  const state: AutoplayState = {
    running: true,
    pieces_placed: 0,
    ticks: 0,
    last_tick_ms: 0,
    last_action: null,
    last_caption: null,
    last_heights: null,
    last_error: null,
    started_at: Date.now(),
    screen_mode: "unknown",
    fast_path_streak: 0,
    phase: "idle",
    phase_started_at: Date.now(),
    recent_ticks: [],
    moondream_calls: 0,
    qwen_calls: 0,
    last_moondream_at: 0,
    claude_calls: 0,
    claude_input_tokens: 0,
    claude_output_tokens: 0,
    claude_spend_usd: 0,
    claude_budget_usd: parseFloat(process.env.CLAUDE_BUDGET_USD ?? "0.50"),
    budget_hit: false,
  };
  const setPhase = (p: Phase) => { state.phase = p; state.phase_started_at = Date.now(); };
  let stopRequested = false;
  let inFlight = false;
  let prevHeights: number[] | null = null;
  let ticksSinceChange = 0;
  let interval: ReturnType<typeof setInterval> | null = null;

  const tick = async () => {
    if (stopRequested) return;
    if (inFlight) return;
    inFlight = true;
    const t0 = Date.now();
    const rec: TickRecord = {
      ts: t0,
      path: "wait",
      total_ms: 0,
      screen_mode: state.screen_mode,
    };
    let captureMs = 0, parseMs = 0, moondreamMs = 0, qwenMs = 0, executeMs = 0;
    try {
      if (!cal?.corners || cal.corners.length !== 4) {
        throw new Error("no matrix calibration — calibrate via the UI");
      }
      if (!streamingCamera.isOpen()) throw new Error("camera not running");
      if (!joypadState.conn) throw new Error("joypad not connected");

      setPhase("capture");
      const tCap = Date.now();
      const { jpeg } = await streamingCamera.getFrame(2000);
      captureMs = Date.now() - tCap;

      setPhase("parse");
      const tParse = Date.now();
      const parsed = await parseMatrix(jpeg, { corners: cal.corners });
      parseMs = Date.now() - tParse;

      const outcome: "advanced" | "no_change" =
        prevHeights && JSON.stringify(prevHeights) === JSON.stringify(parsed.heights)
          ? "no_change"
          : "advanced";
      ticksSinceChange = outcome === "no_change" ? ticksSinceChange + 1 : 0;
      prevHeights = parsed.heights;

      // Three-speed loop:
      //   1. Slow path  (Moondream + Qwen, ~5s) — needed when we don't know
      //      the screen, or every N ticks to revalidate.
      //   2. Repeat path (no LLM, ~500ms) — when we're on a known menu /
      //      game_over / paused screen and heights haven't changed, just
      //      repeat the last button press.
      //   3. Fast path  (parser + heuristic, ~500ms) — confirmed gameplay,
      //      pick the lowest column.
      const FAST_REVALIDATE = 3;   // fire reasoner every ~4th tick during play
      const REPEAT_REVALIDATE = 6;
      const cold = state.ticks === 0;
      const inPlaying = state.screen_mode === "playing";
      const inKnownNonPlaying =
        state.screen_mode === "menu" ||
        state.screen_mode === "game_over" ||
        state.screen_mode === "paused";
      const noProgress = outcome === "no_change";
      const dueFastRevalidate = inPlaying && state.fast_path_streak >= FAST_REVALIDATE;
      const dueRepeatRevalidate = inKnownNonPlaying && state.fast_path_streak >= REPEAT_REVALIDATE;

      // Static mid-board pattern? On the actual game-over results popup,
      // heights show a fixed pattern like [0,0,0,18,18,18,18,0,0,0] — three
      // or more middle columns at exactly the same height while edges are
      // empty. If we see that for 2+ ticks in "playing" mode, it's a lie —
      // re-check via Claude.
      const looksLikePopup = (() => {
        const h = parsed.heights;
        const mid = h.slice(2, 8);
        const allSame = mid.every((x) => x === mid[0]) && mid[0] >= 15;
        const edgesEmpty = h[0] === 0 && h[1] === 0 && h[8] === 0 && h[9] === 0;
        return allSame && edgesEmpty;
      })();
      const popupSuspected = inPlaying && looksLikePopup;

      const useReasoner =
        cold ||
        state.screen_mode === "unknown" ||
        dueFastRevalidate ||
        dueRepeatRevalidate ||
        popupSuspected ||
        (inPlaying && ticksSinceChange >= 3);
      // Repeat is only useful for actions that actually press a button. A
      // cached "wait" would just stall the loop forever on a game-over screen.
      const lastIsActionable =
        state.last_action != null && state.last_action.action !== "wait";
      const useRepeat = !useReasoner && inKnownNonPlaying && noProgress && lastIsActionable;
      // When we're in a known non-playing screen and have no actionable cache
      // (e.g. just transitioned, or reasoner returned wait), fall back to the
      // hardcoded screen→button mapping rather than spinning on "no path".
      const useFallback = !useReasoner && !useRepeat && inKnownNonPlaying;
      const useFast = !useReasoner && !useRepeat && !useFallback && inPlaying;

      let action: QwenAction;
      let caption = state.last_caption;

      if (useReasoner) {
        rec.path = "slow";
        if (USE_CLAUDE) {
          // Hard budget guard. Anthropic API costs add up if a runaway
          // loop fires faster than expected — stop the autoplay outright
          // when the per-session spend exceeds the configured cap.
          if (state.claude_spend_usd >= state.claude_budget_usd) {
            state.budget_hit = true;
            state.last_error = `budget cap hit ($${state.claude_spend_usd.toFixed(3)} ≥ $${state.claude_budget_usd.toFixed(2)}) — stopping`;
            stopRequested = true;
            state.running = false;
            if (interval) clearInterval(interval);
            return;
          }
          // One-shot: Claude does vision + reasoning in a single API call.
          setPhase("qwen");
          const tCl = Date.now();
          const r = await decideWithClaude({
            jpeg,
            game: "tetris",
            heights: parsed.heights,
            last_action: state.last_action ?? undefined,
            ticks_since_change: ticksSinceChange,
          });
          qwenMs = Date.now() - tCl;
          action = r.action;
          caption = `Claude (${r.input_tokens ?? "?"} in / ${r.output_tokens ?? "?"} out tokens, ${r.ms}ms, $${r.cost_usd.toFixed(4)})`;
          state.qwen_calls++;
          state.claude_calls++;
          state.claude_input_tokens += r.input_tokens ?? 0;
          state.claude_output_tokens += r.output_tokens ?? 0;
          state.claude_spend_usd += r.cost_usd;
        } else {
          // Local moondream → qwen fallback (slower, lower quality).
          const MOONDREAM_MIN_INTERVAL_MS = 2000;
          const sinceLast = Date.now() - state.last_moondream_at;
          const heightsChanged = outcome === "advanced";
          const skipMoondream =
            state.last_caption && sinceLast < MOONDREAM_MIN_INTERVAL_MS && !heightsChanged;
          if (!skipMoondream) {
            setPhase("moondream");
            const tMd = Date.now();
            const captionResp = await analyzeImage({
              jpeg,
              prompt: MOONDREAM_PROMPT,
              timeoutMs: 8000,
            }).catch((e) => ({
              text: `(moondream error: ${String(e?.message ?? e).slice(0, 80)})`,
              model: "",
              ms: 0,
            }));
            moondreamMs = Date.now() - tMd;
            caption = captionResp.text;
            state.moondream_calls++;
            state.last_moondream_at = Date.now();
          } else {
            caption = state.last_caption;
          }
          setPhase("qwen");
          const tQw = Date.now();
          action = await decideNext({
            game: "tetris",
            caption: caption ?? "",
            heights: parsed.heights,
            last_action: state.last_action ?? undefined,
            last_action_outcome: outcome,
            ticks_since_change: ticksSinceChange,
          });
          qwenMs = Date.now() - tQw;
          state.qwen_calls++;
        }
        if (action.screen_mode) state.screen_mode = action.screen_mode;
        state.fast_path_streak = 0;
      } else if (useRepeat) {
        rec.path = "repeat";
        action = { ...state.last_action!, reason: `repeat ${state.screen_mode}` };
        state.fast_path_streak++;
      } else if (useFast) {
        rec.path = "fast";
        // Lowest column, but tiebreak toward the spawn column (4) so equal
        // heights don't all migrate to col 0. Tiebreak by absolute distance
        // from spawn → pieces drop straight down when the board is uniform.
        let lo = Infinity, col = PIECE_SPAWN_COL;
        let bestDist = Infinity;
        for (let c = 0; c < 10; c++) {
          const h = parsed.heights[c];
          const d = Math.abs(c - PIECE_SPAWN_COL);
          if (h < lo || (h === lo && d < bestDist)) { lo = h; col = c; bestDist = d; }
        }
        action = { action: "place_piece", screen_mode: "playing", target_col: col, rotation: 0, reason: `fast: lowest col ${col} (h=${lo})` };
        state.fast_path_streak++;
      } else if (useFallback) {
        // Hardcoded mapping per screen. Reasoner already classified — we can
        // press the right button without burning another LLM call.
        rec.path = "repeat";
        const button =
          state.screen_mode === "game_over" ? "B3" : "B1"; // X=RETRY on game_over, A=confirm/resume on menu/paused
        action = {
          action: "press_button",
          buttons: button,
          screen_mode: state.screen_mode,
          reason: `fallback: ${state.screen_mode} → ${button}`,
        };
        state.fast_path_streak++;
      } else {
        rec.path = "wait";
        action = { action: "wait", reason: "no path matched" };
      }

      setPhase("execute");
      const tEx = Date.now();
      await executeAction(action);
      executeMs = Date.now() - tEx;
      if (action.action === "place_piece") state.pieces_placed++;

      state.last_action = action;
      state.last_caption = caption;
      state.last_heights = parsed.heights;
      state.last_error = null;
      rec.action = action;
      rec.caption = caption ?? undefined;
      rec.heights = parsed.heights;
      rec.screen_mode = state.screen_mode;
    } catch (e: any) {
      state.last_error = String(e?.message ?? e);
      rec.error = state.last_error;
    } finally {
      setPhase("idle");
      state.ticks++;
      state.last_tick_ms = Date.now() - t0;
      rec.total_ms = state.last_tick_ms;
      rec.capture_ms = captureMs;
      rec.parse_ms = parseMs;
      rec.moondream_ms = moondreamMs || undefined;
      rec.qwen_ms = qwenMs || undefined;
      rec.execute_ms = executeMs || undefined;
      state.recent_ticks.push(rec);
      if (state.recent_ticks.length > 20) state.recent_ticks.shift();
      inFlight = false;
    }
  };

  interval = setInterval(tick, TICK_MS);
  tick();

  return {
    stop: () => {
      stopRequested = true;
      state.running = false;
      if (interval) clearInterval(interval);
    },
    status: () => state,
  };
}
