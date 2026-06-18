# -*- coding: utf-8 -*-
"""分析 sample0.docx 的 document.xml 结构
输出：
- 总段落数
- 每个段落的关键特征（sectPr, drawing, txbxContent 等）
- 表格信息
"""
import xml.etree.ElementTree as ET
import re
import sys

W_NS = "http://schemas.openxmlformats.org/wordprocessingml/2006/main"

def get_localname(tag):
    if '}' in tag:
        return tag.split('}', 1)[1]
    return tag

def analyze_document(xml_path):
    with open(xml_path, 'r', encoding='utf-8') as f:
        content = f.read()

    # Pretty print for analysis - add newlines between elements
    # Parse the XML
    root = ET.fromstring(content)

    # Find w:body
    body = None
    for child in root:
        if get_localname(child.tag) == 'body':
            body = child
            break

    if body is None:
        print("ERROR: No body found!")
        return

    print("=" * 80)
    print("DOCUMENT.XML ANALYSIS")
    print("=" * 80)

    p_count = 0
    tbl_count = 0
    sectPr_count = 0
    drawing_count = 0
    pict_count = 0
    txbx_count = 0

    body_children = list(body)
    print(f"\nTotal children in <w:body>: {len(body_children)}")
    print("-" * 80)

    for idx, child in enumerate(body_children):
        tag = get_localname(child.tag)

        if tag == 'p':
            p_count += 1
            # Check for sectPr, drawing, pict, txbxContent
            has_sectPr = False
            has_drawing = False
            has_pict = False
            has_txbx = False
            text = ""

            # Check pPr > sectPr
            pPr = child.find(f".//{{{W_NS}}}pPr")
            if pPr is not None:
                sectPr = pPr.find(f"{{{W_NS}}}sectPr")
                if sectPr is not None:
                    has_sectPr = True
                    sectPr_count += 1

            # Check for drawing and pict in all descendants
            for desc in child.iter():
                dtag = get_localname(desc.tag)
                if dtag == 'drawing':
                    has_drawing = True
                    drawing_count += 1
                elif dtag == 'pict':
                    has_pict = True
                    pict_count += 1
                if 'txbxContent' in dtag or 'textbox' in dtag.lower():
                    has_txbx = True
                    txbx_count += 1

            # Collect text content
            for t in child.iter():
                ttag = get_localname(t.tag)
                if ttag == 't' and t.text:
                    text += t.text

            features = []
            if has_sectPr:
                features.append("SECTPR")
            if has_drawing:
                features.append(f"DRAWING(={drawing_count})")
            if has_pict:
                features.append(f"PICT(={pict_count})")
            if has_txbx:
                features.append("TXBX")
            feat_str = f" [{', '.join(features)}]" if features else ""

            text_preview = text[:60]
            print(f"[{idx}] w:p{feat_str} text='{text_preview}{'...' if len(text) > 60 else ''}'")

        elif tag == 'tbl':
            tbl_count += 1
            # Count rows and cells
            rows = 0
            max_cells = 0
            for tr in child:
                tr_tag = get_localname(tr.tag)
                if tr_tag == 'tr':
                    rows += 1
                    cell_count = sum(1 for tc in tr if get_localname(tc.tag) == 'tc')
                    max_cells = max(max_cells, cell_count)
            print(f"[{idx}] w:tbl rows={rows} cols={max_cells}")

        else:
            print(f"[{idx}] <{tag}>")

    print("\n" + "=" * 80)
    print(f"SUMMARY:")
    print(f"  w:p count: {p_count}")
    print(f"  w:tbl count: {tbl_count}")
    print(f"  sectPr count: {sectPr_count}")
    print(f"  drawing count: {drawing_count}")
    print(f"  pict count: {pict_count}")
    print(f"  txbx count: {txbx_count}")
    print("=" * 80)

if __name__ == '__main__':
    xml_path = r"c:\Users\A\lo\libo-core\aproj\docx\samples\sample0_extracted\word\document.xml"
    analyze_document(xml_path)
