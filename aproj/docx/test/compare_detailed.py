#!/usr/bin/env python3
"""
详细对比 lo_nodes 和 aproj_nodes 的 All Nodes 列表
"""
import re

def parse_all_nodes(filepath):
    nodes = []
    with open(filepath, 'r', encoding='utf-8') as f:
        for line in f:
            line = line.strip()
            if line.startswith('# [') and 'All Nodes' not in line:
                m = re.match(r'# \[(\d+)\]\s+(.+)', line)
                if m:
                    nodes.append({'idx': int(m.group(1)), 'info': m.group(2).strip()})
    return nodes

def print_section(nodes, start, end, label):
    print(f"\n{label}:")
    for i in range(start, min(end, len(nodes))):
        n = nodes[i]
        print(f"  [{n['idx']:3d}] {n['info']}")

def main():
    lo_file = r'c:\Users\A\lo\libo-core\aproj\docx\test\lo_nodes.txt'
    ap_file = r'c:\Users\A\lo\libo-core\aproj\docx\test\aproj_nodes.txt'
    
    lo = parse_all_nodes(lo_file)
    ap = parse_all_nodes(ap_file)
    
    print("="*80)
    print(f"LO 节点数: {len(lo)}, AP 节点数: {len(ap)}")
    print(f"LO 节点索引范围: [{lo[0]['idx']}..{lo[-1]['idx']}]")
    print(f"AP 节点索引范围: [{ap[0]['idx']}..{ap[-1]['idx']}]")
    print("="*80)
    
    # 统计各类节点
    def count_types(nodes):
        counts = {}
        for n in nodes:
            info = n['info']
            if 'START_NODE' in info:
                m = re.search(r'type=(\d+)', info)
                t = 'START_NODE type=' + m.group(1) if m else 'START_NODE'
                counts[t] = counts.get(t, 0) + 1
            elif 'TABLE_NODE' in info:
                counts['TABLE_NODE'] = counts.get('TABLE_NODE', 0) + 1
            elif 'GRF_NODE' in info:
                counts['GRF_NODE'] = counts.get('GRF_NODE', 0) + 1
            elif 'TEXT_NODE' in info:
                counts['TEXT_NODE'] = counts.get('TEXT_NODE', 0) + 1
            elif 'END_NODE' in info:
                counts['END_NODE'] = counts.get('END_NODE', 0) + 1
            else:
                counts[info] = counts.get(info, 0) + 1
        return counts
    
    lo_counts = count_types(lo)
    ap_counts = count_types(ap)
    all_types = sorted(set(list(lo_counts.keys()) + list(ap_counts.keys())))
    
    print("\n节点类型统计:")
    print(f"  {'类型':<25} {'LO':>8} {'AP':>8} {'差值':>8}")
    for t in all_types:
        lo_c = lo_counts.get(t, 0)
        ap_c = ap_counts.get(t, 0)
        print(f"  {t:<25} {lo_c:>8} {ap_c:>8} {ap_c - lo_c:>8}")
    
    # 找第一个不同的位置
    print("\n" + "="*80)
    print("逐行对比 - 第一个差异开始:")
    print("="*80)
    max_len = max(len(lo), len(ap))
    first_diff = -1
    for i in range(max_len):
        lo_info = lo[i]['info'] if i < len(lo) else "<MISSING>"
        ap_info = ap[i]['info'] if i < len(ap) else "<MISSING>"
        if lo_info != ap_info:
            first_diff = i
            print(f"\n第一个差异在位置 {i}:")
            if i < len(lo): print(f"  LO[{lo[i]['idx']}]: {lo_info}")
            else: print(f"  LO: <MISSING>")
            if i < len(ap): print(f"  AP[{ap[i]['idx']}]: {ap_info}")
            else: print(f"  AP: <MISSING>")
            # 打印前后几个上下文
            print(f"\n上下文 (位置 {max(0,i-3)}..{min(max_len,i+5)}):")
            for j in range(max(0, i-3), min(max_len, i+5)):
                lo_n = lo[j] if j < len(lo) else None
                ap_n = ap[j] if j < len(ap) else None
                lo_txt = f"LO[{lo_n['idx']:3d}] {lo_n['info']}" if lo_n else "LO: <MISSING>"
                ap_txt = f"AP[{ap_n['idx']:3d}] {ap_n['info']}" if ap_n else "AP: <MISSING>"
                mark = " <<< DIFF" if j == i else ""
                print(f"  {lo_txt:<50} {ap_txt}{mark}")
            break
    
    if first_diff == -1:
        print("完全一致!")

if __name__ == '__main__':
    main()
