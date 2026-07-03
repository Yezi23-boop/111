# Final Artifact Manifest

## Generated Artifacts

- `paper_rewriting_output/final_paper/main.tex`: LaTeX source generated from the official DOCX template structure.
- `paper_rewriting_output/final_paper/main.pdf`: XeLaTeX-compiled PDF draft, currently 21 pages after the edge-AI/refocus and Hermes task-assistance wording update.
- `paper_rewriting_output/final_paper/figures/official_header.png`: official header image extracted from the DOCX template.
- `paper_rewriting_output/final_paper/figures/generated_diagrams/`: generated SVG+PNG structure diagrams for F00, F00B, F00C, F01, F02, F03, F04, F05, F06, and F12; LaTeX references the fixed PNG filenames.
- `paper_rewriting_output/final_paper/preview_completed_figures/`: PNG previews of the earlier 18-page figure pass.
- `paper_rewriting_output/final_paper/preview_latest_report_sync/`: PNG previews of pages 9--12 after syncing the watch endpoint alert path and SD sample recorder flow.
- `paper_rewriting_output/final_paper/preview_latest_report_sync/rewrite_focus-*.png`: PNG previews of pages 1--16 after compressing Hermes and strengthening edge AI / real-machine verification.
- `paper_rewriting_output/final_paper/preview_template_margin_check/`: PNG previews of pages 1--20 after applying the official DOCX page margins.
- `paper_rewriting_output/final_paper/preview_generated_diagrams/`: PNG previews of pages 1--21 after replacing TikZ diagrams with generated PNG structure diagrams.
- `paper_rewriting_output/latex_report.md`: LaTeX assembly and compile report.
- `paper_rewriting_output/figure_asset_map.md`: updated figure status; ten report diagrams are generated as PNG assets under `final_paper/figures/generated_diagrams/`, with F06/F12 synced to the latest repository state.
- `paper_rewriting_output/template_alignment_audit.md`: official DOCX template alignment checklist for sections, word limits, header image, and margins.

## Source Inputs

- Official DOCX template: `C:\Users\ye\Desktop\2026嵌入式大赛应用赛道作品报告模板(1) (1).docx`.
- PaperSpine draft: `paper_rewriting_output/manuscript_draft.md`.
- Section blueprint: `paper_rewriting_output/section_blueprints.md`.
- Writing rationale matrix: `paper_rewriting_output/writing_rationale_matrix.md`.

## Remaining Before Final Submission

- Replace evidence placeholders with actual photos, screenshots, SD sample file screenshots, and test evidence.
- Core report diagrams already generated as PNG assets and visually cleaned: function overview, application scenarios, design process, system architecture, software architecture, edge AI recognition flow, risk state machine, Hermes task-assistance collaboration flow, SD-local data/model optimization loop, and watch endpoint phone notification alert push chain.
- Keep unsupported capabilities out of the completed-results tables; only add new metrics after they have matching logs/screenshots.
- Finalize references within the official limit of 20.
- Export or fill DOCX only after LaTeX content is accepted.
