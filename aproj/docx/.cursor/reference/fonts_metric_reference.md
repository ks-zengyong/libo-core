# Font Metrics Reference — LO vs aproj

> Generated: 2026-06-22 | Task 1 deliverable for font-system-migration

## 1. LO Font Metric Pipeline

### 1.1 Architecture Overview

```
┌─────────────────────────────────────────────────────────────────┐
│  SwTextFormatter (sw/source/core/text/itrform2.cxx)              │
│  CalcRealHeight() — line-level height computation                │
│    ├── m_pCurr->Height() → initial height from portions          │
│    ├── Line spacing rules (Auto/Min/Fix/Prop)                    │
│    ├── Register-true alignment                                   │
│    └── Grid mode handling                                        │
└──────────────┬──────────────────────────────────────────────────┘
               │ calls
┌──────────────▼──────────────────────────────────────────────────┐
│  SwFntObj (sw/source/core/txtnode/fntcache.cxx)                  │
│  Font cache + metric wrapper for Writer                          │
│    ├── GetFontAscent() — from OutputDevice::GetFontMetric()      │
│    ├── GetFontHeight() — from OutputDevice::GetTextHeight()      │
│    ├── GetFontLeading() — manages ext/int leading                │
│    ├── GetTextSize() — text width + height + kern array          │
│    └── lcl_ApplyCjkHeightAdjustment() — CJK font *127/100        │
└──────────────┬──────────────────────────────────────────────────┘
               │ delegates to
┌──────────────▼──────────────────────────────────────────────────┐
│  OutputDevice (vcl/source/outdev/text.cxx, font.cxx)             │
│  Device-level text measurement                                   │
│    ├── GetTextWidth() — total advance width of string            │
│    ├── GetTextHeight() — ascent + descent                        │
│    ├── GetFontMetric() → FontMetricData                          │
│    └── GetTextArray() → KernArray (per-glyph advances)           │
└──────────────┬──────────────────────────────────────────────────┘
               │ triggers lazy init
┌──────────────▼──────────────────────────────────────────────────┐
│  FontMetricData::ImplCalcLineSpacing()                            │
│  (vcl/source/font/fontmetric.cxx:434)                            │
│  **THE CORE FONT METRIC FUNCTION** — HarfBuzz-powered            │
│                                                                  │
│  Algorithm:                                                      │
│  1. Check if variable font (has fvar table)                      │
│     → YES: use hb_ot_metrics_get_position with                   │
│            HB_OT_METRICS_TAG_HORIZONTAL_ASCENDER/DESCENDER/LINE_GAP │
│     → NO:  continue to step 2                                   │
│                                                                  │
│  2. Try hhea table first (mandatory, always present):            │
│     hb_ot_metrics_get_position(pHbFont, ASCENT_HHEA, &nAscent)   │
│     hb_ot_metrics_get_position(pHbFont, DESCENT_HHEA, &nDescent) │
│     hb_ot_metrics_get_position(pHbFont, LINEGAP_HHEA, &nLineGap) │
│     Valid only if nAscent >= 0 && nDescent <= 0                  │
│                                                                  │
│  3. If OS/2 table present, prefer it over hhea:                  │
│     hb_ot_metrics_get_position(pHbFont, ASCENT_OS2, &nTypoAscent)│
│     hb_ot_metrics_get_position(pHbFont, DESCENT_OS2, &nTypoDescent)│
│     hb_ot_metrics_get_position(pHbFont, LINEGAP_OS2, &nTypoLineGap)│
│     hb_ot_metrics_get_position(                                  │
│         HB_OT_METRICS_TAG_HORIZONTAL_CLIPPING_ASCENT, &nWinAscent)│
│     hb_ot_metrics_get_position(                                  │
│         HB_OT_METRICS_TAG_HORIZONTAL_CLIPPING_DESCENT, &nWinDescent)│
│                                                                  │
│     Decision logic:                                              │
│     a) If (hhea empty OR ShouldUseWinMetrics()) → Win metrics    │
│     b) If USE_TYPO_METRICS flag set (fsSelection bit 7) → Typo   │
│     c) Otherwise keep hhea                                      │
│                                                                  │
│  4. Compute in 1/10-pixel units (DPI=8640):                      │
│     fScale = pixelHeight10 / emSize                              │
│     mnAscent = round(fontUnitAscent * fScale) / 6 (→ twips)      │
│     mnDescent = round(fontUnitDescent * fScale) / 6              │
│     mnExtLeading = round(fontUnitExtLeading * fScale) / 6        │
│     mnIntLeading = mnAscent + mnDescent - mnHeight               │
└──────────────────────────────────────────────────────────────────┘
```

### 1.2 Key LO Files

| File | Role |
|------|------|
| `vcl/source/font/fontmetric.cxx` | `FontMetricData::ImplCalcLineSpacing()` — core HarfBuzz-based metrics |
| `vcl/inc/font/FontMetricData.hxx` | `FontMetricData` struct definition |
| `vcl/inc/fontattributes.hxx` | `FontAttributes` base class |
| `vcl/source/outdev/text.cxx` | `OutputDevice::GetTextWidth/GetTextHeight/GetTextArray` |
| `vcl/source/outdev/font.cxx` | `OutputDevice::GetFontMetric` |
| `vcl/source/font/LogicalFontInstance.cxx` | `LogicalFontInstance::GetHbFont()` — HarfBuzz font access |
| `sw/source/core/txtnode/fntcache.cxx` | `SwFntObj` — Writer font cache, `GetFontAscent/Height` |
| `sw/inc/fntcache.hxx` | `SwFntObj` class definition |
| `sw/source/core/text/itrform2.cxx` | `SwTextFormatter::CalcRealHeight()` — line height calc |

### 1.3 Critical Decision: hhea vs OS/2 Win vs OS/2 Typo

LO uses this preference order for non-variable fonts:

1. **hhea** (default) — from `ASCENT_HHEA`/`DESCENT_HHEA`/`LINEGAP_HHEA` private tags
2. **OS/2 Win** — used if hhea empty OR font in `FontsUseWinMetrics` config list
3. **OS/2 Typo** — used if `USE_TYPO_METRICS` flag (fsSelection bit 7) is set

The CJK height adjustment (`*127/100`) is applied on top via `lcl_ApplyCjkHeightAdjustment()`.

### 1.4 SwTextFormatter::CalcRealHeight Line Height Logic

```cpp
// itrform2.cxx:2264 — entry
SwTwips nLineHeight = m_pCurr->Height(); // initial from text portions

// Line spacing rules:
// Auto:    use font's natural height (default)
//          + PROP_LINE_SPACING_SHRINKS_FIRST_LINE for first line
// Min:     max(nLineHeight, specified minimum)
// Fix:     fixed line height, ascent = 80% of line height
// Prop:    nLineHeight += (nPropPercent - 100) * textHeight / 100

// Register-true: align to grid (if IsRegisterOn())
// Grid mode: snap to grid lines

m_pCurr->SetRealHeight(nLineHeight); // final line height
```

---

## 2. aproj Current State

### 2.1 Font Engine Architecture

```
FontEngine (singleton, font_engine.cpp:705)
├── FontInstance cache (fontName → FontInstance*)
├── Path cache (font name → file path)
├── AltName cache (fontTable altName substitutions)
└── Measures:
    ├── GetTextHeight() → HarfBuzz (LO-compatible) ✅
    ├── GetTextWidth() → stb_truetype + GDI ⚠️ NOT LO-COMPATIBLE
    ├── GetCharWidth() → stb_truetype ⚠️ NOT LO-COMPATIBLE
    └── GetTextBreak() → GDI + stb_truetype fallback ⚠️
```

### 2.2 Already Correct (HarfBuzz-based)

`FontInstance::GetTextHeight()` (font_engine.cpp:436):
- HarfBuzz blob → face → font
- `hb_ot_metrics_get_position()` for hhea (ASCENT_HHEA/DESCENT_HHEA/LINEGAP_HHEA)
- `hb_ot_metrics_get_position()` for OS/2 (ASCENT_OS2/DESCENT_OS2/LINEGAP_OS2)
- Win metrics check via `ShouldUseWinMetrics()`
- `USE_TYPO_METRICS` flag handling (fsSelection bit 7)
- CJK font detection via ulCodePageRange1 bits
- Variable font support (fvar table check)
- 1/10-pixel precision (DPI=8640) → twips conversion
- CJK height adjustment (*127/100)
- ADD_EXT_LEADING support

### 2.3 Remaining Gaps

| Function | Currently Uses | Should Use | Impact |
|----------|---------------|------------|--------|
| `GetTextWidth()` | stb_truetype + GDI | HarfBuzz `hb_font_get_glyph_h_advance()` | Text width differences → line break differences |
| `GetCharWidth()` | stb_truetype | HarfBuzz `hb_font_get_glyph_h_advance()` | Individual char width differences |
| `GetTextBreak()` | GDI `GetTextExtentPoint32W` + stbtt | HarfBuzz shaped text width | Line break point differences |
| `GetMetric()` | stb_truetype `GetFontVMetrics` | HarfBuzz (same as GetTextHeight) | Legacy, may still be called |

### 2.4 HarfBuzz Availability

- **Source**: `third_party/harfbuzz/harfbuzz.cc` (full HarfBuzz single-file build)
- **Build**: Compiled as static library `harfbuzz` via CMakeLists.txt:53-57
- **Headers**: All HB headers available in `third_party/harfbuzz/`
- **Link**: `docx_core` links against `harfbuzz` (CMakeLists.txt:96)

---

## 3. Frame Diff Correlation

### 3.1 Font Metric → Frame Field Mapping

| Frame Field | Source Function | Metric Source |
|-------------|----------------|---------------|
| `TEXT_FRAME.h` (height) | `SwTextFormatter::CalcRealHeight()` | `SwFntObj::GetFontHeight()` → `OutputDevice::GetTextHeight()` → `FontMetricData::ImplCalcLineSpacing()` |
| `TEXT_FRAME.w` (width) | `SwFntObj::GetTextSize()` | `OutputDevice::GetTextArray()` → HarfBuzz glyph advances |
| `TEXT_FRAME.y` (position) | Cumulative line heights | All above |
| `TEXT_LINE.h` | Same as TEXT_FRAME.h | HarfBuzz ascent+descent |
| Font name/size in log | `RenderLog` printing | Style chain resolution |

### 3.2 Known Metric Differences (from font-metric-gap memory)

| Font | Size | stb_truetype | LO (HarfBuzz) | Diff |
|------|------|-------------|---------------|------|
| Segoe UI Semibold | 36pt | 478 | 508 | +30 |
| Poppins | 24pt | 256 | 258 | +2 |
| fony family | 24pt | 292 | 359 | +67 |
| Calibri | 20pt | ~244 | ~479 | ~235 |

### 3.3 Root Cause Summary

1. **stb_truetype vs HarfBuzz table selection**: stb_truetype uses hhea as-is; LO HarfBuzz prefers OS/2 Win metrics for some fonts
2. **CJK height adjustment**: LO applies *127/100 for CJK fonts; stb_truetype doesn't
3. **Glyph advance calculation**: stbtt uses simple hmtx; HarfBuzz may apply OpenType features
4. **Text width/break**: GDI vs HarfBuzz shaping differences affect line breaks

---

## 4. Migration Strategy Recommendation

### Plan A: Full HarfBuzz Integration (Recommended)

Replace stb_truetype text width/break with HarfBuzz-based shaping:

1. `GetTextWidth()`: Use `hb_buffer` + `hb_shape()` + `hb_buffer_get_glyph_positions()` 
2. `GetCharWidth()`: Use `hb_font_get_glyph_h_advance()`  
3. `GetTextBreak()`: Use `hb_buffer` + `hb_shape()` + binary search on glyph positions
4. `GetMetric()`: Already done via `GetTextHeight()`

### Plan B: Calibration Table (Not Recommended)

Map stb_truetype results to LO HarfBuzz results via a calibration table. Rejected in prior analysis (memory: font-metric-gap.md) — LO heights include line wrapping context, not fixed per font/size.

### Implementation Priority

1. **GetTextWidth via HarfBuzz** — highest impact, affects all text frames
2. **GetTextBreak via HarfBuzz** — affects line break decisions
3. **Remove stb_truetype dependency for metrics** — cleanup after migration
