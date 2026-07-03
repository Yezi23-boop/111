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
- Ten report diagrams are generated as 2048x1152 PNG structure diagrams under `final_paper/figures/generated_diagrams/` and referenced from LaTeX: function overview, application scenarios, design process, system architecture, software architecture, edge AI recognition flow, risk state machine, Hermes task-assistance flow, data/model optimization loop, and phone notification alert push chain.
- 2026-07-04 diagram asset update: the earlier TikZ figure bodies were replaced with `\includegraphics[width=0.95\linewidth]{figures/generated_diagrams/*.png}` while keeping existing captions and section text. Editable SVG sources and `generate_diagrams.py` are kept next to the PNG outputs for later visual refinements.
- 2026-07-04 update: the data/model loop and phone notification chain were synced with the latest repository state. The main phone notification path now uses ESP32 Alerting -> watch_endpoint_service -> HTTPS POST `/v1/watch/alerts` -> watch endpoint -> Android notification, while OneNET/WeChat is no longer presented as the current implementation.
- 2026-07-04 update: the data loop now describes the implemented SD sample recorder stage: continuous 16kHz PCM tap, `window_end_sample_index` aligned capture, and pre/post 1s WAV+JSON written to `/sdcard/danger_samples`; later optimization is phrased as manual export, offline labeling, false-positive analysis, and retraining, without claiming audio cloud upload.
- 2026-07-04 privacy wording update: tightened danger-sample wording to local SD storage, manual export, offline labeling, and offline optimization; F06 now uses `人工导出` and `离线优化`.
- 2026-07-04 template audit: the official DOCX was parsed directly; section coverage, word-limit sections, header image hash, and page margins were checked. `main.tex` now uses the DOCX template margins and the verified official header image.
- 2026-07-04 content refocus: the report was revised to foreground ESP32-S3 edge AI danger-sound recognition, local risk state machine, local alerting, SD sample capture, and real-machine/log evidence. Hermes is now described as a non-safety-critical task-assistance and controlled task-execution layer for users with accessibility needs rather than a primary competition throughline.
- 2026-07-04 Hermes positioning update: report prose now states that Hermes can help users record tasks, create reminders, query information, and coordinate authorized computer-side tasks, while safety recognition and local alerting remain on ESP32-S3.
- TikZ diagrams were simplified after visual review: edge labels were removed or moved into prose, long node text was shortened, and the software module table now uses readable Chinese module names instead of broken long identifiers.
- Remaining figure positions for hardware photos, UI screenshots, phone notification screenshots, Hermes callback screenshots, and SD sample file screenshots are automatic-numbered evidence placeholders because final evidence images have not been supplied yet.
- Tables are generated for technical characteristics, performance indicators, software modules, outcomes, feature tests, phone notification push responsibilities, and parameter summary.

## Guard Checks

| Checkpoint | Errors | Warnings | Fixed? |
| --- | ---: | ---: | --- |
| Initial LaTeX compile | 0 | Table overfull/underfull warnings | Yes, adjusted table indentation and long module names |
| Final LaTeX compile after TikZ diagrams | 0 | Minor underfull warnings only, no fatal errors | Acceptable for current draft |
| Visual preview after diagram cleanup | 0 | No obvious figure text overlap on pages 6, 7, 8, 10, and 11 | Checked with PNG renders in `final_paper/preview_completed_figures/` |
| Phone notification push section update | 0 | Superseded by latest repository sync | Earlier OneNET -> WeChat design retained only as optional extension in prose |
| Repository-state sync update | 0 | Minor underfull warnings only, no fatal errors | Replaced OneNET-first chain with current watch endpoint alert path and added SD sample recorder evidence |
| Placeholder figure numbering cleanup | 0 | Minor underfull warnings only, no fatal errors | Converted evidence placeholders to real LaTeX figures so numbering remains continuous |
| Official template alignment audit | 0 | Minor underfull warnings only, no fatal errors | Margins/header/sections/word limits checked; report recorded in `template_alignment_audit.md` |
| Generated PNG diagram replacement | 0 | Minor underfull warnings only, no fatal errors | Ten TikZ structure/flow diagrams replaced by generated PNG assets; evidence placeholders unchanged; pages 2--13 visually previewed |
| Edge-AI/refocus rewrite | 0 | Minor underfull warnings only, no Overfull after shortening the state-machine row | Hermes compressed to non-safety-critical task assistance; pages 1--16 visually previewed in `final_paper/preview_latest_report_sync/` |
| Hermes task-assistance wording update | 0 | Minor underfull warnings only, no fatal errors or Overfull warnings | `main.tex` and supporting asset notes now align Hermes with accessibility task recording and controlled task execution |

## Compilation

- Engine: XeLaTeX
- Command: `xelatex -interaction=nonstopmode -halt-on-error -file-line-error main.tex`
- Status: success
- PDF: `paper_rewriting_output/final_paper/main.pdf`
- Current PDF length: 21 pages after the edge-AI/refocus rewrite.
- Latest visual preview: pages 1--16 rendered with Poppler to `paper_rewriting_output/final_paper/preview_latest_report_sync/rewrite_focus-*.png`; generated diagrams and rewritten tables render without obvious overflow.

## Content Integrity

- All official template sections present: yes
- Figures present: ten generated PNG structure diagrams plus placeholders for final evidence images
- Tables present: yes
- Citations resolved: reference list is plain numbered text, no BibTeX used yet
- Known author tasks: add hardware photos, danger UI screenshots, phone notification screenshots, SD sample file screenshots, board logs, Hermes callback screenshots, watch endpoint alert logs, measured metrics, and final references before submission
