#!/usr/bin/env python3
"""完整解析 lo_nodes.txt 的结构并构建节点树"""
import re

lo_file = r'c:\Users\A\lo\libo-core\aproj\docx\test\lo_nodes.txt'
ap_file = r'c:\Users\A\lo\libo-core\aproj\docx\test\aproj_nodes.txt'

def parse_nodes(filepath):
    nodes = []
    with open(filepath, 'r', encoding='utf-8') as f:
        for line in f:
            line = line.strip()
            if line.startswith('# [') and 'All Nodes' not in line:
                m = re.match(r'# \[(\d+)\]\s+(.+)', line)
                if m:
                    idx = int(m.group(1))
                    info = m.group(2).strip()
                    type_ = None
                    if 'START_NODE' in info:
                        m2 = re.search(r'type=(\d+)', info)
                        type_ = 'START_' + m2.group(1) if m2 else 'START_0'
                    elif 'END_NODE' in info:
                        type_ = 'END'
                    elif 'GRF_NODE' in info:
                        type_ = 'GRF'
                    elif 'TABLE_NODE' in info:
                        type_ = 'TABLE'
                    elif 'TEXT_NODE' in info:
                        type_ = 'TEXT'
                    nodes.append({'idx': idx, 'type': type_, 'info': info})
    return nodes

def build_tree(nodes):
    tree = []
    stack = []
    current_children = tree
    for n in nodes:
        if n['type'] and n['type'].startswith('START_'):
            node = {'idx': n['idx'], 'type': n['type'], 'children': []}
            current_children.append(node)
            stack.append(current_children)
            current_children = node['children']
        elif n['type'] == 'END':
            if stack:
                current_children = stack.pop()
            else:
                current_children.append({'idx': n['idx'], 'type': 'END_ORPHAN'})
        else:
            current_children.append({'idx': n['idx'], 'type': n['type']})
    return tree

def print_tree(tree, depth=0):
    for node in tree:
        prefix = '  ' * depth
        t = node.get('type', '?')
        idx = node['idx']
        if 'children' in node:
            cc = len(node['children'])
            print(prefix + '[' + str(idx).rjust(3) + '] ' + t + ' (' + str(cc) + ' children)')
            print_tree(node['children'], depth+1)
        else:
            print(prefix + '[' + str(idx).rjust(3) + '] ' + t)

print("="*80)
print("LO 节点树结构:")
print("="*80)
lo_nodes = parse_nodes(lo_file)
lo_tree = build_tree(lo_nodes)
print_tree(lo_tree[:15])

print("\n" + "="*80)
print("AP 节点树结构:")
print("="*80)
ap_nodes = parse_nodes(ap_file)
ap_tree = build_tree(ap_nodes)
print_tree(ap_tree[:15])

# 详细统计
def count_summary(tree):
    result = []
    for node in tree:
        if 'children' in node:
            cc = len(node['children'])
            result.append('[' + str(node['idx']).rjust(3) + '] ' + node['type'] + ' -> ' + str(cc) + ' sub-items')
            child_summary = count_summary(node['children'])
            for cs in child_summary:
                result.append('  ' + cs)
    return result

print("\n" + "="*80)
print("LO 节区统计:")
print("="*80)
for line in count_summary(lo_tree):
    print(line)

print("\n" + "="*80)
print("AP 节区统计:")
print("="*80)
for line in count_summary(ap_tree):
    print(line)
