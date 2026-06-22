# Frame Diff Font-Metric Analysis

> Generated: 2026-06-22 | Task 2 deliverable

## Current State

| Metric | Value |
|--------|-------|
| Node diff | 0 ✅ |
| Frame diff | 148 |
| Frame instruction count | 207 vs 207 (match) |
| Page count | LO=7, aproj=5? |

## Diff Categories

### Category 1: Font Height Metric Mismatches (~40 diffs)

**Root cause**: `GetTextHeight()` uses HarfBuzz but results differ from LO.

Key examples:
| Ref# | Font | Size | LO Height | aproj Height | Δ |
|------|------|------|-----------|--------------|---|
| 2 | Segoe UI Semibold | 36pt | 508 | 479 | 29 |
| 6 | (Share para) | — | 336 | 355 | 19 |
| 7 | (Poppins para) | — | 359 | 355 | 4 |
| 10 | Segoe UI Semibold | 36pt | 508 | 479 | 29 |
| 11 | Segoe UI Semibold? | — | 258 | 277 | 19 |
| 12 | (body text) | — | 772 | 553 | 219 |
| 13 | (major layout) | — | 1640 | 946 | 694 |
| 55,59-68 | Column lines | — | 338 | 276 | 62 |

**Known causes**:
1. Font file path resolution — aproj may load different version of font files
2. TTC face selection — for `segoeui.ttc` (4 faces), aproj selects by max upem which may differ from LO/GDI
3. HarfBuzz version incompatibility — different table preference logic

### Category 2: Text Width Measurement (~10 diffs)

**Root cause**: `GetTextWidth()` uses stb_truetype + GDI, NOT HarfBuzz.

Key examples:
| Ref# | LO Width | aproj Width | Δ |
|------|----------|-------------|---|
| 49-55 | 5019 | 5020 | 1 |
| 59-68 | 5019 | 5020 | 1 |
| 56-57 | 5113 | 11878 | 6765 |

**Why this matters**: Text width determines line breaks, which affect:
- Line count per paragraph
- Paragraph height
- Page break positions
- Y-position cascading

### Category 3: Font Name/Size/Style Resolution (~5 diffs)

**Root cause**: Font resolution for paragraph mark vs style font.

Key examples:
| Ref# | Field | LO | aproj |
|------|-------|----|-------|
| 133 | fontName | Poppins SemiBold | Calibri |
| 133 | fontSize | 40 | 20 |
| 197 | fontSize | 44 | 20 |
| 197 | fontColor | 0 | 16777215 |

### Category 4: Column/Pagination Layout (~30 diffs)

**Root cause**: Missing page 5-7 content, column width calculation.

Key examples:
- `ref@56-57`: Column body width 5113 vs 11878 (unconstrained)
- `ref@131`: Width 10466 vs 8306 (narrower column)
- Page positions: Multiple pages shifted/missing

### Category 5: Cascaded Y Diffs (~60 diffs)

**Root cause**: Accumulated height differences cascading page positions.
- Most y-position diffs are downstream of Category 1 (height) and Category 2 (width → line count)

---

## Function-to-Diff Mapping

| Diff Category | Current Function | Should Use | Priority |
|--------------|-----------------|------------|----------|
| Height (Cat 1) | `GetTextHeight()` — HB already | Debug why differs from LO | P0 |
| Width (Cat 2) | `GetTextWidth()` — stbtt+GDI | HarfBuzz `hb_shape()` + glyph positions | P0 |
| TextBreak (Cat 2) | `GetTextBreak()` — GDI+stbtt | HarfBuzz `hb_shape()` + binary search | P0 |
| Font resolution (Cat 3) | Path cache | Better font matching | P1 |
| Pagination (Cat 4) | `frmtree.cpp` layout | Debug page structure | P1 |

---

## Fix Approach Decision

### Decision: **HarfBuzz text width/break migration** (extends existing HarfBuzz integration)

**Rationale**:
1. HarfBuzz is already compiled and linked in the project
2. `GetTextHeight()` already uses HarfBuzz (though needs debugging for LO parity)
3. `GetTextWidth()` and `GetTextBreak()` are the last stb_truetype holdouts
4. This is the pure "migrate LO logic" approach per project rules

### Implementation Plan (→ Task 3)

1. **Fix GetTextWidth**: Use HarfBuzz shaping API:
   ```cpp
   hb_buffer → hb_shape → hb_buffer_get_glyph_positions → sum advances
   ```
   This replaces both stb_truetype and GDI for width measurement.

2. **Fix GetTextBreak**: Use HarfBuzz shaping + binary search on glyph positions:
   ```cpp
   hb_shape entire text → glyph positions → binary search for break point
   ```

3. **Debug GetTextHeight LO parity**: 
   - Compare aproj font file vs LO font file resolution
   - Add debug logging for table selection (hhea/OS2 Win/Typo)
   - Verify TTC face selection matches GDI

4. **Regression verify**: build → gen_lo/gen_aproj → diff_node/diff_frame
