import os
import re

src_dir = '/data/data/com.termux/files/home/downloads/mousiki-main/src'
for root, _, files in os.walk(src_dir):
    for f in sorted(files):
        if not f.endswith(('.h', '.cpp')): continue
        path = os.path.join(root, f)
        with open(path, 'r', encoding='utf-8', errors='ignore') as file:
            content = file.read()
            # Extract classes and top-level functions/methods
            classes = re.findall(r'(?:class|struct)\s+([A-Za-z0-9_]+)', content)
            print(f"File: {f}")
            if classes:
                print(f"  Classes/Structs: {', '.join(set(classes))}")
            
            # Print first 5 lines of comments if present at the top
            lines = content.split('\n')
            comments = []
            for line in lines:
                line = line.strip()
                if line.startswith('//') or line.startswith('/*') or line.startswith('*'):
                    comments.append(line)
                elif line and not line.startswith('#'):
                    break
                if len(comments) >= 5:
                    break
            if comments:
                print(f"  Top Comments: {' | '.join(comments)}")
            print("-" * 40)
