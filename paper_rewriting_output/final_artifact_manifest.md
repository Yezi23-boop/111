# Final Artifact Manifest

## Generated Artifacts

- `paper_rewriting_output/final_paper/main.tex`: LaTeX source generated from the official DOCX template structure.
- `paper_rewriting_output/final_paper/main.pdf`: XeLaTeX-compiled PDF draft, rebuilt after UP-template alignment, abstract compression, evidence-placeholder cleanup, platform-utilization wording, current deployed model metrics, model structure, training/deployment platform, board evidence, App/phone supplemental alert alignment, official-heading strict alignment, and the simplified 3.3 特性成果 rewrite.
- `paper_rewriting_output/final_paper/paper.docx`: Word draft generated from the current LaTeX report content; available diagrams and hardware images are embedded, and evidence placeholders are retained as Word table boxes.
- `paper_rewriting_output/final_paper/figures/official_header.png`: official header image extracted from the DOCX template.
- `paper_rewriting_output/final_paper/figures/generated_diagrams/`: generated SVG+PNG structure diagrams for F00, F00B, F00C, F01, F02, F03, F04, F05, F06, and F12; LaTeX references the fixed PNG filenames. F01/F02/F05/F06/F12 use reviewer-facing terms such as 云服务端、危险告警、后台发送.
- `paper_rewriting_output/final_paper/figures/hardware/`: hardware design images extracted from the user-provided smart-watch DOCX, including the system schematic, PCB outline, PCB top layout, and PCB bottom layout.
- `paper_rewriting_output/final_paper/preview_completed_figures/`: PNG previews of the earlier 18-page figure pass.
- `paper_rewriting_output/final_paper/preview_latest_report_sync/`: PNG previews of pages 9--12 after syncing the cloud-service alert path and SD local sample-saving flow.
- `paper_rewriting_output/final_paper/preview_latest_report_sync/rewrite_focus-*.png`: PNG previews of pages 1--16 after compressing Hermes and strengthening edge AI / real-machine verification.
- `paper_rewriting_output/final_paper/preview_hardware_merge_final/`: PNG previews of pages 8--13 after merging the user-provided hardware schematic, PCB outline, and PCB layout images.
- `paper_rewriting_output/final_paper/preview_reviewer_wording/`: PNG previews used for the reviewer-facing terminology pass, including the refreshed page 15 risk-state/table check.
- `paper_rewriting_output/final_paper/preview_template_margin_check/`: PNG previews of pages 1--20 after applying the official DOCX page margins.
- `paper_rewriting_output/final_paper/preview_generated_diagrams/`: PNG previews of pages 1--21 after replacing TikZ diagrams with generated PNG structure diagrams.
- `paper_rewriting_output/final_paper/preview_app_alert_alignment/`: PNG previews of selected pages after reframing the report around local safety alert plus App/phone supplemental alert and adding the PCB real-photo placeholder.
- `paper_rewriting_output/final_paper/preview_official_heading_alignment/`: PNG previews of selected pages after merging non-template numbered headings back into the official section structure.
- `paper_rewriting_output/final_paper/preview_3_3_rewrite/`: PNG previews of pages 21--26 after simplifying 3.3 特性成果.
- `paper_rewriting_output/latex_report.md`: LaTeX assembly and compile report.
- `paper_rewriting_output/word_report.md`: Word conversion report with paragraph/table/media counts and residue check.
- `paper_rewriting_output/figure_asset_map.md`: updated figure status; ten report diagrams are generated as PNG assets under `final_paper/figures/generated_diagrams/`, with F06/F12 synced to the latest repository state and reviewer-facing terminology. Remaining evidence is compressed into 6 P0 group figures plus P1 optional evidence.
- `paper_rewriting_output/template_alignment_audit.md`: official DOCX template alignment checklist for sections, word limits, header image, and margins.
- Local AudioClassification-Pytorch evaluation report: current deployed model metrics source integrated into the report as tables, covering background/horn/siren only. The formal report wording avoids internal model iteration labels and local paths.

## Source Inputs

- Official DOCX template: used as a local formatting reference; formal submission source does not expose its local path.
- User hardware-content DOCX: used as the local source for schematic and PCB design figures; formal submission source does not expose its local path.
- PaperSpine draft: `paper_rewriting_output/manuscript_draft.md`.
- Section blueprint: `paper_rewriting_output/section_blueprints.md`.
- Writing rationale matrix: `paper_rewriting_output/writing_rationale_matrix.md`.
- Current deployed model evaluation source: local AudioClassification-Pytorch text report and JSON report, recorded internally only for provenance.

## Remaining Before Final Submission

- Replace the 6 P0 evidence placeholders with actual group figures: full-device photo collage, PCB real-photo collage, danger-alert/haptic collage, sensitivity/background safety-listening screenshot, App/phone notification chain collage, and SD sample-saving collage.
- The newly merged schematic and PCB layout images support hardware design completion, but final submission still needs real hardware photos and PCB real-photo evidence.
- Latest chapter 3 prose now records SD sample serial board-test evidence and cloud-service auth/deduplication tests; final submission still needs matching screenshots/log excerpts to replace placeholders.
- Latest report prose also records the implemented sensitivity strategy, haptic alert task, and low-power safety-listening orchestration; final submission should show them through the consolidated sensitivity/background safety-listening group figure and the danger-alert/haptic group figure, not through scattered standalone screenshots.
- Latest Hermes prose positions Hermes as the project companion personal AI Agent service and the watch as its wearable interaction/control terminal. Final submission should use one real Hermes callback/reminder/task-summary screenshot; computer-side tool execution evidence can be added after matching real-machine verification.
- P1 evidence: add a 24 h stability log summary if available. Current deployed model metrics already exist in the report as tables; a separate model-evaluation figure is optional for visual presentation. P2 evidence is handled through merged evidence groups: mechanical annotation placeholder, standalone haptic photo, multiple Hermes screenshots, standalone server-test screenshot, and repeated device photos.
- Core report diagrams already generated as PNG assets and visually cleaned: function overview, application scenarios, design process, system architecture, software architecture, edge AI recognition flow, risk state machine, Hermes task-assistance collaboration flow, SD-local data/model optimization loop, and cloud-service App/phone alert chain.
- Current report treats family/guardian alert reception as an application extension built on the App/phone supplemental alert chain; it is not claimed as an already tested family-binding feature.
- Wearable weight, battery capacity, standby/listening power, and continuous endurance are marked as pending real measurements and should only be filled after matching evidence exists.
- Formal section headings now follow the official template; detailed validation and metric evidence is consolidated under `3.3 特性成果` rather than separate `3.4` / `3.5` subsections.
- Keep unsupported capabilities out of the completed-results tables; only add new metrics after they have matching logs/screenshots.
- Finalize references within the official limit of 20.
- Export or fill DOCX only after LaTeX content is accepted.
