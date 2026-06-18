#!/usr/bin/env python3
"""Check page/section breaks in sample0.docx paragraphs."""
import re
import zipfile
from pathlib import Path

DOCX = Path(__file__).resolve().parents[2] / "samples" / "sample0.docx"

def main():
    with zipfile.ZipFile(DOCX) as z:
        xml = z.read("word/document.xml").decode("utf-8")

    paras = re.findall(r"<w:p[ >].*?</w:p>", xml, re.DOTALL)
    for i, p in enumerate(paras):
        text = re.sub(r"<[^>]+>", "", p)
        text = text.replace("&amp;", "&").strip()[:70]
        flags = []
        pb = re.search(r"w:pageBreakBefore[^/]*/>", p)
        if pb:
            tag = pb.group(0)
            if 'w:val="0"' not in tag and 'w:val="false"' not in tag:
                flags.append("pageBreakBefore")
        if "sectPr" in p:
            flags.append("sectPr")
        if re.search(r'w:br[^>]*w:type="page"', p):
            flags.append("pageBr")
        if flags or "WPS AI" in text or "fony" in text.lower():
            print(f"{i:3d} {flags!s:30s} {text!r}")

if __name__ == "__main__":
    main()
