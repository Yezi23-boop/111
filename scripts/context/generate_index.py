import os
import re
from pathlib import Path
import warnings

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
CONTEXT_DIR = REPO_ROOT / "docs" / "context"
INDEX_FILE = CONTEXT_DIR / "INDEX.agent.md"

def parse_frontmatter(raw: str) -> tuple[dict, str]:
    """Parse YAML frontmatter from raw markdown string."""
    meta = {}
    body = raw
    if raw.startswith("---\n"):
        parts = raw.split("\n---\n", 1)
        if len(parts) == 2:
            fm_text = parts[0][4:]
            body = parts[1]
            for line in fm_text.split("\n"):
                line = line.strip()
                if not line or line.startswith("#"):
                    continue
                if ":" in line:
                    k, v = line.split(":", 1)
                    k = k.strip()
                    v = v.strip()
                    if v.startswith('"') and v.endswith('"'):
                        v = v[1:-1]
                    elif v.startswith("'") and v.endswith("'"):
                        v = v[1:-1]
                    meta[k] = v
    return meta, body

def find_route_files() -> list:
    """Scan context directory for markdown files with route_area in frontmatter."""
    routes = []
    # Only search in specific dirs to avoid noise, or search entire context dir
    search_dirs = [
        CONTEXT_DIR / "knowledge",
        CONTEXT_DIR / "plans",
        CONTEXT_DIR / "runs",
        CONTEXT_DIR / "procedures"
    ]
    
    for d in search_dirs:
        if not d.exists():
            continue
        for p in d.rglob("*.md"):
            try:
                raw = p.read_text(encoding="utf-8")
                meta, _ = parse_frontmatter(raw)
                if "route_area" in meta:
                    rel_path = p.relative_to(REPO_ROOT).as_posix()
                    owners = meta.get("owners", "")
                    routes.append({
                        "area": meta["route_area"],
                        "owners": owners,
                        "path": rel_path
                    })
            except Exception as e:
                print(f"Error reading {p}: {e}")
                
    return routes

def generate_table(routes: list) -> str:
    # Sort by area
    routes.sort(key=lambda x: x["area"])
    
    lines = [
        "| Area | Owners | Start with |",
        "| --- | --- | --- |"
    ]
    for r in routes:
        owners_str = r["owners"]
        if owners_str and "`" not in owners_str:
            # simple comma separated
            owners = [o.strip() for o in owners_str.split(',')]
            owners_str = ", ".join(f"`{o}`" for o in owners if o)
            
        lines.append(f"| {r['area']} | {owners_str} | `{r['path']}` |")
        
    return "\n".join(lines)

def update_index_file(table_str: str):
    if not INDEX_FILE.exists():
        print(f"Error: {INDEX_FILE} not found.")
        return
        
    content = INDEX_FILE.read_text(encoding="utf-8")
    
    start_marker = "<!-- DOMAIN_ROUTING_START -->"
    end_marker = "<!-- DOMAIN_ROUTING_END -->"
    
    if start_marker in content and end_marker in content:
        # replace between markers
        before = content.split(start_marker)[0]
        after = content.split(end_marker)[1]
        new_content = before + start_marker + "\n" + table_str + "\n" + end_marker + after
        INDEX_FILE.write_text(new_content, encoding="utf-8")
        print("Updated INDEX.agent.md successfully.")
    else:
        print("Markers not found in INDEX.agent.md. Cannot auto-update.")

def main():
    print("Scanning for route areas...")
    routes = find_route_files()
    print(f"Found {len(routes)} routes.")
    if not routes:
        return
        
    table_str = generate_table(routes)
    update_index_file(table_str)

if __name__ == "__main__":
    main()
