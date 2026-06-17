#!/usr/bin/env python3
"""
精准对比 lo_nodes.txt 和 aproj_nodes.txt 的 All Nodes 列表部分
"""
import re
import sys

def parse_all_nodes(filepath):
    """解析文件的 All Nodes 部分，返回节点列表"""
    nodes = []
    with open(filepath, 'r', encoding='utf-8') as f:
        lines = f.readlines()
    
    in_all_nodes = False
    for line in lines:
        line = line.rstrip('\n')
        if 'All Nodes' in line:
            in_all_nodes = True
            continue
        if in_all_nodes and line.startswith('# Body Area'):
            break
        if in_all_nodes and line.startswith('# ['):
            # 提取节点信息: # [idx] TYPE extra=...
            m = re.match(r'# \[(\d+)\]\s+(.+)', line)
            if m:
                idx = int(m.group(1))
                info = m.group(2).strip()
                nodes.append({'idx': idx, 'info': info})
    return nodes

def parse_overview(filepath):
    """解析 Overview 部分"""
    overview = {}
    with open(filepath, 'r', encoding='utf-8') as f:
        for line in f:
            line = line.strip()
            if 'Total nodes' in line:
                m = re.search(r'Total nodes:\s+(\d+)', line)
                if m: overview['total'] = int(m.group(1))
            elif 'BodyStart' in line:
                m = re.search(r'BodyStart:\s+(\d+)', line)
                if m: overview['bodystart'] = int(m.group(1))
            elif 'BodyEnd' in line:
                m = re.search(r'BodyEnd:\s+(\d+)', line)
                if m: overview['bodyend'] = int(m.group(1))
            if 'All Nodes' in line:
                break
    return overview

def main():
    lo_file = r'c:\Users\A\lo\libo-core\aproj\docx\test\lo_nodes.txt'
    ap_file = r'c:\Users\A\lo\libo-core\aproj\docx\test\aproj_nodes.txt'
    
    lo_nodes = parse_all_nodes(lo_file)
    ap_nodes = parse_all_nodes(ap_file)
    lo_overview = parse_overview(lo_file)
    ap_overview = parse_overview(ap_file)
    
    print("=" * 80)
    print("OVERVIEW 对比")
    print("=" * 80)
    print(f"LO:   {lo_overview}")
    print(f"AP:   {ap_overview}")
    print()
    
    max_len = max(len(lo_nodes), len(ap_nodes))
    print("=" * 80)
    print(f"节点差异对比 (共 {max_len} 个位置，LO:{len(lo_nodes)}, AP:{len(ap_nodes)})")
    print("=" * 80)
    
    diff_count = 0
    for i in range(max_len):
        lo_node = lo_nodes[i] if i < len(lo_nodes) else None
        ap_node = ap_nodes[i] if i < len(ap_nodes) else None
        
        lo_info = lo_node['info'] if lo_node else "<MISSING>"
        ap_info = ap_node['info'] if ap_node else "<MISSING>"
        
        if lo_info != ap_info:
            diff_count += 1
            lo_idx = lo_node['idx'] if lo_node else '?'
            ap_idx = ap_node['idx'] if ap_node else '?'
            print(f"\n位置 {i} (LO[{lo_idx}] vs AP[{ap_idx}]):")
            print(f"  LO: {lo_info}")
            print(f"  AP: {ap_info}")
    
    print("\n" + "=" * 80)
    print(f"总计差异数: {diff_count}")
    print("=" * 80)

if __name__ == '__main__':
    main()
