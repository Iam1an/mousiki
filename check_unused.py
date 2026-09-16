import os
import re

src_dir = '/data/data/com.termux/files/home/downloads/mousiki-main/src'
cpp_files = [f for f in os.listdir(src_dir) if f.endswith('.cpp')]
h_files = [f for f in os.listdir(src_dir) if f.endswith('.h')]

all_content = {}
for f in cpp_files + h_files:
    with open(os.path.join(src_dir, f), 'r', encoding='utf-8', errors='ignore') as file:
        all_content[f] = file.read()

for f in cpp_files:
    base = f[:-4]
    header = base + '.h'
    # Find if this file or its header is included anywhere else
    used = False
    for other, content in all_content.items():
        if other in (f, header): continue
        if re.search(r'#include\s+["<]' + re.escape(header) + r'[">]', content) or \
           re.search(r'#include\s+["<]' + re.escape(f) + r'[">]', content):
            used = True
            break
    if not used and f != 'main.cpp':
        print(f"Potentially unused: {f} (and {header})")
