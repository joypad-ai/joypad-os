// Tetris matrix parser. Given a JPEG frame and the four calibrated corners
// of the matrix (in source pixels: TL, TR, BR, BL), sample a 10×20 grid of
// cell colors. Classify each cell as empty or filled. Return pile heights.
//
// Fast path — no neural net. Uses sharp to decode the JPEG into a raw RGB
// buffer once, then point-samples each cell center using bilinear interp
// across the calibrated quad. ~5–15 ms per frame on M-series.

import sharp from "sharp";

export interface ParsedMatrix {
  // 10×20 grid, row-major: cells[row][col]. row 0 = top, row 19 = bottom.
  cells: boolean[][];
  // Height of stack in each column. heights[col] = count of filled rows from
  // the bottom (0 = empty column).
  heights: number[];
  // Coarse colour averages per cell — useful for a future piece-type pass.
  rgb?: [number, number, number][][];
  ms: number;
}

export interface MatrixCalibration {
  corners: [number, number][];   // [TL, TR, BR, BL] in source-image pixels
  source_size?: { w: number; h: number };
}

const COLS = 10;
const ROWS = 20;

// Bilinear interpolation across the calibrated quad. uv in [0,1]^2,
// (0,0)=TL, (1,0)=TR, (1,1)=BR, (0,1)=BL.
function quadAt(corners: [number, number][], u: number, v: number): [number, number] {
  const [tl, tr, br, bl] = corners;
  const top: [number, number] = [tl[0] + (tr[0] - tl[0]) * u, tl[1] + (tr[1] - tl[1]) * u];
  const bot: [number, number] = [bl[0] + (br[0] - bl[0]) * u, bl[1] + (br[1] - bl[1]) * u];
  return [top[0] + (bot[0] - top[0]) * v, top[1] + (bot[1] - top[1]) * v];
}

export async function parseMatrix(jpeg: Buffer, cal: MatrixCalibration): Promise<ParsedMatrix> {
  const t0 = Date.now();
  const img = sharp(jpeg).raw().ensureAlpha();
  const { data, info } = await img.toBuffer({ resolveWithObject: true });
  const W = info.width;
  const H = info.height;
  const channels = info.channels; // 4 with ensureAlpha

  const corners = cal.corners;
  const cells: boolean[][] = [];
  const rgbGrid: [number, number, number][][] = [];

  // Cell centers are at u = (col+0.5)/COLS, v = (row+0.5)/ROWS.
  for (let r = 0; r < ROWS; r++) {
    const row: boolean[] = [];
    const rgbRow: [number, number, number][] = [];
    const v = (r + 0.5) / ROWS;
    for (let c = 0; c < COLS; c++) {
      const u = (c + 0.5) / COLS;
      const [px, py] = quadAt(corners, u, v);
      const xi = Math.max(0, Math.min(W - 1, Math.round(px)));
      const yi = Math.max(0, Math.min(H - 1, Math.round(py)));
      const idx = (yi * W + xi) * channels;
      const R = data[idx], G = data[idx + 1], B = data[idx + 2];
      rgbRow.push([R, G, B]);
      row.push(isFilled(R, G, B));
    }
    cells.push(row);
    rgbGrid.push(rgbRow);
  }

  // Pile heights: scan each column from the bottom up. Height = number of
  // filled cells from row=19 inclusive going upward, stopping at the first
  // empty row. (We tolerate small holes in the middle of the pile — a
  // strict count from the topmost-filled is more honest about height.)
  const heights = new Array(COLS).fill(0);
  for (let c = 0; c < COLS; c++) {
    for (let r = 0; r < ROWS; r++) {
      if (cells[r][c]) { heights[c] = ROWS - r; break; }
    }
  }

  return { cells, heights, rgb: rgbGrid, ms: Date.now() - t0 };
}

// Classify a cell as empty (matrix background) vs filled.
//
// The Tetris Ultimate matrix background is dark blue. Saturation alone is a
// terrible signal — dark blue has very high saturation (R is near 0). The
// honest distinguisher is luminance: empty cells are dim, pieces are bright.
function isFilled(R: number, G: number, B: number): boolean {
  // Rec. 601 luma. Pieces > 100, empty cells typically < 60 in our captures.
  const luma = 0.299 * R + 0.587 * G + 0.114 * B;
  return luma > 90;
}
