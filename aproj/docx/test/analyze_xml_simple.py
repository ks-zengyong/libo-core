#!/usr/bin/env python3
"""分析 document.xml 中每个段落的内容和 drawing 类型"""
import zipfile
from xml.etree import ElementTree as ET

docx_path = r'c:\Users\A\lo\libo-core\aproj\docx\samples\sample0.docx'

with zipfile.ZipFile(docx_path) as zf:
    with zf.open('word/document.xml') as f:
        content = f.read().decode('utf-8')

root = ET.fromstring(content)

namespaces = {}
# 遍历所有节点来查找带 drawing/textbox/pic 的元素
def get_tag(elem):
    tag = elem.tag
    if '}' in tag:
        return tag.split('}')[1]
    return tag

body = None
for elem in root.iter():
    if get_tag(elem) == 'body':
        body = elem
        break

print("="*80)
print("Body 子元素分析:")
print("="*80)

if body is None:
    print("No body found, searching all p/tbl in document")
    body = root

child_idx = 0
for child in list(body):
    tag = get_tag(child)
    
    if tag == 'p':
        # 收集段落文本
        text_parts = []
        for t in child.iter():
            if get_tag(t) == 't' and t.text:
                text_parts.append(t.text)
        para_text = ''.join(text_parts)
        
        # 检查 drawing 类型
        desc_tags = [get_tag(d) for d in child.iter()]
        has_drawing = 'drawing' in desc_tags or 'pict' in desc_tags
        has_pic = 'pic' in desc_tags
        has_wsp = 'wsp' in desc_tags or 'shape' in [d.lower() for d in desc_tags]
        has_sectPr = 'sectPr' in desc_tags
        
        # 检查 txbxContent 并收集文本
        txbx_texts = []
        in_txbx = False
        txbx_iter = None
        for d in child.iter():
            dt = get_tag(d)
            if dt == 'txbxContent' or dt == 'textbox':
                in_txbx = True
                txbx_texts.append('')
                continue
            if in_txbx:
                if dt == 'p' and txbx_texts:
                    if txbx_texts[-1] == '' and len(txbx_texts) > 0:
                        pass
                    else:
                        txbx_texts.append('')
                elif dt == 't' and d.text:
                    if txbx_texts:
                        txbx_texts[-1] += d.text
        
        # 清理空的最后一个
        while txbx_texts and txbx_texts[-1] == '':
            txbx_texts.pop()
        
        info = f"w:p (idx={child_idx}) text='{para_text[:50]}'"
        draw_info = ""
        if has_drawing:
            parts = []
            if has_pic:
                parts.append("PIC")
            if has_wsp or txbx_texts:
                parts.append(f"WSP/TEXTBOX({len(txbx_texts)} paras)")
            if parts:
                draw_info = " [" + ", ".join(parts) + "]"
            else:
                draw_info = " [drawing]"
        if has_sectPr:
            info += " [sectPr]"
        print(info + draw_info)
        
        if txbx_texts:
            for i, tt in enumerate(txbx_texts):
                print(f"    txbx[{i}]: '{tt[:80]}'")
    
    elif tag == 'tbl':
        rows = [d for d in child.iter() if get_tag(d) == 'tr']
        row_count = len(rows)
        col_count = 0
        if rows:
            first_row_cells = [d for d in rows[0].iter() if get_tag(d) == 'tc']
            col_count = len(first_row_cells)
        
        print(f"w:tbl (idx={child_idx}) {row_count} rows x {col_count} cols")
        
        # 打印单元格内容
        for ri, row in enumerate(rows):
            cells = [d for d in row.iter() if get_tag(d) == 'tc']
            for ci, cell in enumerate(cells):
                cell_text = ''
                for t in cell.iter():
                    if get_tag(t) == 't' and t.text:
                        cell_text += t.text
                if cell_text.strip():
                    print(f"    [{ri},{ci}]: '{cell_text[:60]}'")
    
    child_idx += 1

print(f"\n总计: {child_idx} 个子元素")
