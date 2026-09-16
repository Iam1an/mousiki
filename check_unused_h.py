import os
import re

src_dir = '/data/data/com.termux/files/home/downloads/mousiki-main/src'
cpp_files = [f for f in os.listdir(src_dir) if f.endswith('.cpp')]
h_files = [f for f in os.listdir(src_dir) if f.endswith('.h')]

all_content = {}
for f in cpp_files + h_files:
    with open(os.path.join(src_dir, f), 'r', encoding='utf-8', errors='ignore') as file:
        all_content[f] = file.read()

for header in h_files:
    used = False
    for other, content in all_content.items():
        if other == header: continue
        if re.search(r'#include\s+["<]' + re.escape(header) + r'[">]', content):
            used = True
            break
    if not used:
        print(f"Potentially unused: {header}")
