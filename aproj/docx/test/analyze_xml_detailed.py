#!/usr/bin/env python3
"""
分析 sample0.docx 的 document.xml 中每个 w:p/w:tbl 的内容和 drawing 类型
"""
import zipfile
import io
from xml.etree import ElementTree as ET

docx_path = r'c:\Users\A\lo\libo-core\aproj\docx\samples\sample0.docx'

# 从docx中提取document.xml
with zipfile.ZipFile(docx_path) as zf:
    with zf.open('word/document.xml') as f:
        content = f.read().decode('utf-8')

# 去掉命名空间以便分析
import re
# 简化：用 lxml 风格或正则分析

# 使用 pugixml 风格的手动解析
import xml.etree.ElementTree as ET

# 注册命名空间
namespaces = {
    'w': 'http://schemas.openxmlformats.org/wordprocessingml/2006/main',
    'wp': 'http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing',
    'pic': 'http://schemas.openxmlformats.org/drawingml/2006/picture',
    'wps': 'http://schemas.microsoft.com/office/word/2010/wordprocessingShape',
    'a': 'http://schemas.openxmlformats.org/drawingml/2006/main',
    'r': 'http://schemas.openxmlformats.org/officeDocument/2006/relationships',
}

root = ET.fromstring(content)

body = root.find('.//w:body', namespaces)

print("="*80)
print("Body 子元素分析:")
print("="*80)

child_idx = 0
for child in list(body):
    tag = child.tag.split('}')[-1]
    if tag == 'p':
        # 段落
        para_text_parts = []
        for r_elems = child.findall('.//w:r', namespaces)
        for r in r_elems:
            t_elems = r.findall('.//w:t', namespaces)
            for t in t_elems:
                if t.text:
                    para_text_parts.append(t.text)
        para_text = ''.join(para_text_parts)
        
        # 检查 drawing
        has_drawing = False
        drawing_types = []
        
        # 检查所有 drawing 元素
        all_descendants = list(child.iter())
        for desc_list = []
        for desc_tags = set()
        for d in child.iter():
            dt = d.tag.split('}')[-1] if '}' in d.tag else d.tag
            desc_tags.add(dt)
        
        # 特别检查 drawing/pic/wsp
        has_pic = False
        has_wsp = False
        has_txbxContent = False
        txbx_texts = []
        
        for d in child.iter():
            dt = d.tag.split('}')[-1] if '}' in d.tag else d.tag
            if dt == 'pic':
                has_pic = True
            if dt == 'wsp':
                has_wsp = True
            if dt == 'txbxContent':
                has_txbxContent = True
                # 收集 txbxContent 内的文本
                for p_in_txbx in d.iter():
                    pt = p_in_txbx.tag.split('}')[-1] if '}' in p_in_txbx.tag else p_in_txbx.tag
                    if pt == 'p':
                        t = ''
                        for t_elem in p_in_txbx.iter():
                            tt = t_elem.tag.split('}')[-1] if '}' in t_elem.tag else t_elem.tag
                            if tt == 't' and t_elem.text:
                                t += t_elem.text
                        txbx_texts.append(t)
        
        has_drawing = 'drawing' in desc_tags or 'pict' in desc_tags
        
        # 检查 sectPr
        has_sectPr = False
        for d in child.iter():
            dt = d.tag.split('}')[-1] if '}' in d.tag else d.tag
            if dt == 'sectPr':
                has_sectPr = True
                break
        
        info = f"w:p (idx={child_idx}, text='{para_text[:50]}'"
        if has_drawing:
            info += f", drawing="
            if has_pic:
                info += "PIC"
            if has_wsp:
                info += f"+WSP(textbox)"
            if has_txbxContent:
                info += f"+txbxContent({len(txbx_texts)}paras)"
        info += ")"
        
        if has_sectPr:
            info += " [sectPr]"
        
        print(info)
        if has_txbxContent and txbx_texts:
            for i, tt in enumerate(txbx_texts):
                print(f"    txbx[{i}]: '{tt[:80}")
        
    elif tag == 'tbl':
        # 表格
        rows = child.findall('.//w:tr', namespaces)
        row_count = len(rows)
        # 计算列数（第一行的单元格数
        col_count = 0
        if rows:
            cells = rows[0].findall('.//w:tc', namespaces)
            col_count = len(cells)
        print(f"w:tbl (idx={child_idx}, {row_count} rows x {col_count} cols)")
        # 打印单元格内容
        for ri, row in enumerate(rows[:2):
            cells = row.findall('.//w:tc', namespaces)
            for ci, cell in enumerate(cells):
                cell_text = ''
                for t in cell.iter():
                    tt = t.tag.split('}')[-1] if '}' in t.tag else t.tag
                    if tt == 't' and t.text:
                        cell_text += t.text
                if cell_text.strip():
                    print(f"    [{ri},{ci}]: '{cell_text[:60]}")
    child_idx += 1

print(f"\n总计: {child_idx} 个子元素")
