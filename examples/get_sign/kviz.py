import subprocess
import re
import os
import json

# 查找最新的输出目录
def get_latest_output_dir(keyword):
    command = "./../../build/bin/klee -debug-print-instructions=all:stderr -write-smt2s -write-exec-tree get_sign.bc"
    process = subprocess.Popen(command, shell=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    
    latest_line = None
    
    for line in iter(process.stdout.readline, b''):
        decoded_line = line.decode("utf-8")
        
        if keyword in decoded_line:
            latest_line = decoded_line.strip()
    
    if latest_line:
        
        # 使用正则表达式匹配目标值
        pattern = r'{}-(\d+)'.format(keyword)
        matches = re.search(pattern, latest_line)

        if matches:
            target_value = matches.group(0)
            return target_value
    else:
        return None

# 在下面调用函数并传入要检查的关键字
output_dir=get_latest_output_dir("klee-out")
print(output_dir)

def extract_source_file():
    # 检查目录是否存在
    if not os.path.isdir(output_dir):
        print("目录不存在：", output_dir)
        return None
    
    assembly_file_path = os.path.join(output_dir, "assembly.ll")
    print(assembly_file_path)
    # 检查文件是否存在
    if not os.path.isfile(assembly_file_path):
        print("文件不存在：", assembly_file_path)
        return None
    
    with open(assembly_file_path, 'r') as file:
        lines = file.readlines()
        if len(lines) >= 2:
            source_file = lines[1].strip()

            # 提取文件名
            start_index = source_file.find('"') + 1
            end_index = source_file.rfind('"')
            source_file = source_file[start_index:end_index]
            return source_file
        else:
            print("文件不足两行：", assembly_file_path)
            return None

# 提取 "assembly.ll" 文件第二行的源文件名
source_filename = extract_source_file()

if not os.path.isfile(source_filename):
    print("文件不存在：", source_filename)
else:
    with open(source_filename, 'r') as file:
        content = file.read()
        # 写入 JSON 文件
        json_file_path = "source_code.json"
        with open(json_file_path, 'w') as json_file:
            json_file.write(content)
        print("文件内容已写入json文件。")

command = "../../build/bin/klee-exec-tree tree-dot {}".format(output_dir)
process = subprocess.Popen(command, shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

lines = process.stdout.read().decode("utf-8").strip().split('\n')  # 按行分割并去除首尾空白字符
filtered_lines = [line for line in lines if '->' in line]  # 保留包含箭头的行

filtered_output = '\n'.join(filtered_lines)  # 将保留的行连接为字符串

print(filtered_output)

def parse_output(lines):
    nodes = {}
    edges = []

    for line in lines:
        if '->{' in line:
            parts = line.split('->{')
            parent = parts[0].strip()
            children = parts[1].strip().rstrip('};').split()
            edges.append((parent, children))

    print(edges)

    for parent, children in edges:
        if parent not in nodes:
            nodes[parent] = {'name': parent, 'children': []}
        for child in children:
            if child not in nodes:
                nodes[child] = {'name': child, 'children': []}
            nodes[parent]['children'].append(nodes[child])

    # 找到根节点
    root = None
    for node in nodes.values():
        if not any(node in edges for _, children in edges for child in children if child == node['name']):
            root = node
            break

    return root

lines = filtered_output.split('\n')
root_node = parse_output(lines)

# 将根节点写入 JSON 文件
with open('tree.json', 'w') as file:
    json.dump(root_node, file, indent=4, separators=(',', ':'))

