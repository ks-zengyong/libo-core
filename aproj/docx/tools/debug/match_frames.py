#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Match TEXT_FRAME lines between lo_frame.txt and aproj_frame.txt by text content,
then report height/y/x differences to find systematic patterns.

Reads relative to the docx test dir. Run from aproj/docx.
"""
import re
import sys
from pathlib import Path


def parse_frames(path):
    """Return list of dicts for TEXT_FRAME rows: page,x,y,w,h,text,font,size."""
    frames = []
    with open(path, encoding='utf-8') as f:
        for line in f:
            line = line.rstrip('\n').rstrip('\r')
            s = line.lstrip(' ')
            if not s.startswith('TEXT_FRAME'):
                continue
            # tokenize respecting quoted field
            # format: TEXT_FRAME \t page \t x \t y \t w \t h \t "text" \t font \t size ...
            toks = []
            i = 0
            n = len(s)
            while i < n:
                while i < n and s[i] == ' ':
                    i += 1
                if i >= n:
                    break
                if s[i] == '"':
                    k = i + 1
                    raw = ''
                    while k < n:
                        if s[k] == '\\' and k + 1 < n:
                            raw += s[k:k+2]
                            k += 2
                            continue
                        if s[k] == '"':
                            k += 1
                            break
                        raw += s[k]
                        k += 1
                    toks.append(raw)
                    i = k
                    if i < n and s[i] == '\t':
                        i += 1
                else:
                    j = s.find('\t', i)
                    if j < 0:
                        toks.append(s[i:].strip())
                        i = n
                    else:
                        toks.append(s[i:j].strip())
                        i = j + 1
            if len(toks) >= 6:
                try:
                    frames.append({
                        'page': int(toks[1]),
                        'x': int(toks[2]),
                        'y': int(toks[3]),
                        'w': int(toks[4]),
                        'h': int(toks[5]),
                        'text': toks[6] if len(toks) > 6 else '',
                        'font': toks[7] if len(toks) > 7 else '',
                        'size': int(toks[8]) if len(toks) > 8 else 0,
                    })
                except ValueError:
                    pass
    return frames


def main():
    test_dir = Path(__file__).resolve().parent.parent / 'test'
    lo = parse_frames(test_dir / 'lo_frame.txt')
    ap = parse_frames(test_dir / 'aproj_frame.txt')

    # Build aproj index by (text, font, size) -> list
    ap_index = {}
    for f in ap:
        key = (f['text'], f['font'], f['size'])
        ap_index.setdefault(key, []).append(f)

    print(f"LO frames: {len(lo)}, aproj frames: {len(ap)}")
    print("=" * 100)

    matched = 0
    consumed = set()
    rows = []
    for lf in lo:
        key = (lf['text'], lf['font'], lf['size'])
        cands = ap_index.get(key, [])
        chosen = None
        for c in cands:
            if id(c) not in consumed:
                chosen = c
                consumed.add(id(c))
                break
        if chosen is None:
            continue
        matched += 1
        rows.append((lf, chosen))

    print(f"Matched by (text,font,size): {matched}")
    print()
    print(f"{'font':<22}{'sz':>3} {'loH':>5}{'apH':>5}{'dH':>5}  "
          f"{'loX':>6}{'apX':>6}{'dX':>5}  text")
    print("-" * 100)
    for lf, af in rows:
        dh = af['h'] - lf['h']
        dx = af['x'] - lf['x']
        flag = '  <<< DIFF' if (dh or dx) else ''
        t = lf['text'][:40]
        print(f"{lf['font']:<22}{lf['size']:>3} {lf['h']:>5}{af['h']:>5}{dh:>+5}  "
              f"{lf['x']:>6}{af['x']:>6}{dx:>+5}  {t}{flag}")


if __name__ == '__main__':
    main()
