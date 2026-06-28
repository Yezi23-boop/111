# LaTeX Report

## Baseline

- Original project compiled: not applicable; source was generated from the official DOCX report template and PaperSpine manuscript draft.
- Target template: `C:\Users\ye\Desktop\2026嵌入式大赛应用赛道作品报告模板(1) (1).docx`.
- Template structure: paragraph-only official application-track report template; no embedded tables.

## Assembly

- Output LaTeX: `paper_rewriting_output/final_paper/main.tex`.
- Chinese engine/template: `ctexart` with XeLaTeX.
- Structure follows official template: work name, abstract, five major parts, references.
- Official header image extracted from the DOCX template to `paper_rewriting_output/final_paper/figures/official_header.png` and applied through `fancyhdr`.
- Five core design diagrams are generated directly in LaTeX with TikZ: system architecture, edge AI recognition flow, risk state machine, Hermes collaboration flow, and data/model optimization loop.
- TikZ diagrams were simplified after visual review: edge labels were removed or moved into prose, long node text was shortened, and the software module table now uses readable Chinese module names instead of broken long identifiers.
- Remaining figure positions for hardware photos, UI screenshots, and test plots are placeholders because final evidence images have not been supplied yet.
- Tables are generated for technical characteristics, performance indicators, software modules, outcomes, feature tests, and parameter summary.

## Guard Checks

| Checkpoint | Errors | Warnings | Fixed? |
| --- | ---: | ---: | --- |
| Initial LaTeX compile | 0 | Table overfull/underfull warnings | Yes, adjusted table indentation and long module names |
| Final LaTeX compile after TikZ diagrams | 0 | Minor underfull warnings only, no fatal errors | Acceptable for current draft |
| Visual preview after diagram cleanup | 0 | No obvious figure text overlap on pages 4, 6, 7, and 8 | Checked with PNG renders in `final_paper/preview2/` |

## Compilation

- Engine: XeLaTeX
- Command: `xelatex -interaction=nonstopmode -halt-on-error -file-line-error main.tex`
- Status: success
- PDF: `paper_rewriting_output/final_paper/main.pdf`
- Current PDF length: 11 pages after adding the official header and five TikZ diagrams.
- Latest visual preview: pages 4--9 rendered with Poppler to `paper_rewriting_output/final_paper/preview2/`.

## Content Integrity

- All official template sections present: yes
- Figures present: five generated design diagrams plus placeholders for final evidence images
- Tables present: yes
- Citations resolved: reference list is plain numbered text, no BibTeX used yet
- Known author tasks: add hardware photos, UI screenshots, board logs, Hermes logs, measured metrics, and final references before submission
