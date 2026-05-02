// Minimal Tetris heuristic. V1: pick the column with the lowest stack —
// drop hits the floor or fills a valley. Doesn't yet account for piece
// shape, rotation, or holes; that's V2 work once we can identify the
// falling piece reliably.

export interface HeuristicChoice {
  target_col: number;   // 0–9 from the left
  // How confident we are this is meaningful (0 if pile is flat, 1 if
  // there's a clear valley). Lets the loop skip a turn rather than fight
  // an unparseable frame.
  confidence: number;
  reason: string;
}

export function pickColumn(heights: number[]): HeuristicChoice {
  if (heights.length !== 10) {
    return { target_col: 4, confidence: 0, reason: "bad heights array" };
  }
  let minH = Infinity;
  let bestCol = 4;
  for (let c = 0; c < 10; c++) {
    if (heights[c] < minH) { minH = heights[c]; bestCol = c; }
  }
  const maxH = Math.max(...heights);
  const range = maxH - minH;
  return {
    target_col: bestCol,
    confidence: Math.min(1, range / 4),
    reason: `min height ${minH} in col ${bestCol}, range ${range}`,
  };
}
