import os
import re
from pathlib import Path

INDEX_FILE = Path("D:/esp32S3/111/docs/context/INDEX.agent.md")
REPO_ROOT = Path("D:/esp32S3/111")

def main():
    if not INDEX_FILE.exists():
        print("INDEX.agent.md not found")
        return

    content = INDEX_FILE.read_text(encoding="utf-8")
    
    # Extract table rows
    in_table = False
    routes = []
    lines = content.split('\n')
    for line in lines:
        if line.startswith("## Domain Routing"):
            in_table = True
            continue
        if in_table and line.startswith("## "):
            break
        if in_table and line.startswith("|") and not line.startswith("| Area") and not line.startswith("| ---"):
            parts = [p.strip() for p in line.split('|')]
            if len(parts) >= 4:
                area = parts[1]
                owners = parts[2]
                path_str = parts[3].strip('`')
                if path_str:
                    routes.append((area, owners, path_str))
                    
    print(f"Found {len(routes)} routes.")
    
    for area, owners, path_str in routes:
        file_path = REPO_ROOT / path_str
        if not file_path.exists():
            print(f"File not found: {file_path}")
            continue
            
        file_content = file_path.read_text(encoding="utf-8")
        
        # Regex to find frontmatter
        match = re.match(r"^---\n(.*?)\n---\n(.*)", file_content, flags=re.DOTALL)
        if not match:
            print(f"No frontmatter found in {file_path}")
            continue
            
        frontmatter = match.group(1)
        body = match.group(2)
        
        if "route_area:" not in frontmatter:
            # append it before the end of frontmatter
            new_frontmatter = frontmatter.rstrip() + f'\nroute_area: "{area}"\n'
            new_content = f"---\n{new_frontmatter}---\n{body}"
            file_path.write_text(new_content, encoding="utf-8")
            print(f"Added route_area to {file_path}")
        else:
            print(f"Already has route_area: {file_path}")

if __name__ == "__main__":
    main()
