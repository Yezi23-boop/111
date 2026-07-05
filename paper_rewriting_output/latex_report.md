# LaTeX Report

## Baseline

- Original project compiled: not applicable; source was generated from the official DOCX report template and PaperSpine manuscript draft.
- Target template: official 2026 embedded application-track report DOCX template, used only as a local formatting reference.
- Template structure: paragraph-only official application-track report template; no embedded tables.

## Assembly

- Output LaTeX: `paper_rewriting_output/final_paper/main.tex`.
- Chinese engine/template: `ctexart` with XeLaTeX.
- Structure follows official template: work name, abstract, five major parts, references.
- Official header image extracted from the DOCX template to `paper_rewriting_output/final_paper/figures/official_header.png` and applied through `fancyhdr`.
- Ten report diagrams are generated as 2048x1152 PNG structure diagrams under `final_paper/figures/generated_diagrams/` and referenced from LaTeX: function overview, application scenarios, design process, system architecture, software architecture, edge AI recognition flow, risk state machine, Hermes task-assistance flow, data/model optimization loop, and App/phone supplemental alert chain.
- 2026-07-04 diagram asset update: the earlier TikZ figure bodies were replaced with `\includegraphics[width=0.95\linewidth]{figures/generated_diagrams/*.png}` while keeping existing captions and section text. Editable SVG sources and `generate_diagrams.py` are kept next to the PNG outputs for later visual refinements.
- 2026-07-04 update: the data/model loop and App/phone supplemental alert chain were synced with the latest repository state. The alert path is described only as the current implementation: ESP32-S3 手表确认危险 -> 本地屏幕/震动提醒 -> 后台任务上报告警 -> 云服务端转发 -> App/手机通知栏提醒.
- 2026-07-04 update: the data loop now describes the implemented SD local sample-saving stage in reviewer-facing terms: local audio cache, pre/post 1s WAV+JSON saved on SD card, manual export, offline labeling, false-positive analysis, and retraining, without claiming audio cloud upload.
- 2026-07-04 privacy wording update: tightened sample-saving wording to local SD storage, manual export, offline labeling, and offline optimization; F06 now uses `人工导出` and `离线优化`.
- 2026-07-04 template audit: the official DOCX was parsed directly; section coverage, word-limit sections, header image hash, and page margins were checked. `main.tex` now uses the DOCX template margins and the verified official header image.
- 2026-07-04 content refocus: the report was revised to foreground ESP32-S3 edge AI danger-sound recognition, local risk state machine, local alerting, SD sample saving, and real-machine/log evidence. Hermes is now described as a non-safety-critical task-assistance and controlled task-execution layer for users with accessibility needs rather than a primary competition throughline.
- 2026-07-04 Hermes positioning update: report prose now states that Hermes can help users record tasks, create reminders, query information, and coordinate authorized computer-side tasks, while safety recognition and local alerting remain on ESP32-S3.
- 2026-07-05 Hermes wearable-control update: report prose now defines Hermes as the project companion personal AI Agent service with long-term memory, task understanding, tool use, cross-device collaboration, and companionship-style interaction. The watch is described as Hermes' wearable interaction/control terminal for submitting tasks away from the computer and receiving callbacks, reminders, and task summaries, while remaining outside danger-sound recognition and local alert decisions.
- 2026-07-05 evidence update: chapter 3 now records the latest repository evidence for SD local sample saving and phone alert delivery. SD sample saving is upgraded from source-test-only wording to source tests plus serial board-test evidence. Phone alert delivery now emphasizes FreeRTOS queued background sending, timeout-protected HTTPS alert requests, and cloud-service auth/deduplication tests.
- 2026-07-05 repository-state update: the report now reflects the latest completed firmware capabilities: three sensitivity modes mapped to ESP-DL thresholds `0.95/0.90/0.85`, initial danger haptic alert, and the background low-power safety-listening mechanism. These capabilities are written as implemented modules, while sustained alert rhythm, event review, power quantification, and offline model optimization remain future work.
- 2026-07-05 terminology cleanup: report-facing wording changed the internal English module label to `后台安全监听` / `低功耗安全监听机制`, and replaced code-oriented resource phrases with reviewer-readable Chinese wording.
- 2026-07-05 reviewer-wording update: internal code-oriented labels and exact sample-file details were replaced with reviewer-facing wording such as 云服务端、本地音频缓存、SD 本地样本保存、后台发送任务. Generated SVG/PNG diagrams were regenerated with the same terminology.
- 2026-07-05 hardware-content update: hardware system content was merged from the user-provided smart-watch hardware DOCX. Four user-provided hardware design images were extracted to `final_paper/figures/hardware/` and referenced from LaTeX: system schematic, PCB outline, PCB top layout, and PCB bottom layout.
- TikZ diagrams were simplified after visual review: edge labels were removed or moved into prose, long node text was shortened, and the software module table now uses readable Chinese module names instead of broken long identifiers.
- Remaining evidence placeholders are compressed into 6 P0 group figures: full-device photo collage, PCB real-photo collage, danger-alert/haptic collage, sensitivity/background safety-listening screenshot, App/phone notification chain collage, and SD sample-saving collage. Hermes callback, 24 h stability evidence, and model evaluation are P1; standalone haptic photos, mechanical annotation images, repeated Hermes screenshots, standalone server-test screenshots, and duplicate device photos are handled through merged evidence groups.
- Current deployed model metrics from the local AudioClassification-Pytorch evaluation report are integrated into sections 1.4 and 3.3 特性成果. The formal report wording uses 当前部署模型 / 稳定部署模型. The verified model-metric scope is background/horn/siren only; the report records 96.18% accuracy, 93.83% danger recall, 0.00% false alarm rate, 100.00% precision, 96.82% F1-score, the binary confusion matrix, per-source-class statistics, and board resource/timing results.
- 2026-07-05 UP-template alignment pass: abstract was compressed into background/problem/core-function/verification wording under the 800-character limit; chapter 3 now uses formal evidence figure names instead of draft workflow labels; platform utilization wording now explicitly covers ESP32-S3, ESP-DL, FreeRTOS, LVGL, SD storage, audio input, display, and haptic hardware; formal LaTeX source contains no local file paths or internal model iteration labels.
- Tables are generated for technical characteristics, performance indicators, software modules, outcomes, feature tests, sensitivity/haptic/background safety-listening status, App/phone supplemental alert responsibilities, and parameter summary.

## Guard Checks

| Checkpoint | Errors | Warnings | Fixed? |
| --- | ---: | ---: | --- |
| Initial LaTeX compile | 0 | Table overfull/underfull warnings | Yes, adjusted table indentation and long module names |
| Final LaTeX compile after TikZ diagrams | 0 | Minor underfull warnings only, no fatal errors | Acceptable for current draft |
| Visual preview after diagram cleanup | 0 | No obvious figure text overlap on pages 6, 7, 8, 10, and 11 | Checked with PNG renders in `final_paper/preview_completed_figures/` |
| Phone notification push section update | 0 | Superseded by latest repository sync | Phone notification wording now presents only the current cloud-service alert path |
| Repository-state sync update | 0 | Minor underfull warnings only, no fatal errors | Replaced earlier alternate chain wording with the current cloud-service alert path and added SD local sample-saving evidence |
| Chapter 3 evidence-structure cleanup | 0 | Minor underfull table wrapping only, no fatal errors or Overfull warnings | Reworked the feature verification table around 功能现象 -> 测试方法 -> 实测结果 -> 证据图/材料 |
| Placeholder figure numbering cleanup | 0 | Minor underfull warnings only, no fatal errors | Converted evidence placeholders to real LaTeX figures so numbering remains continuous |
| Official template alignment audit | 0 | Minor underfull warnings only, no fatal errors | Margins/header/sections/word limits checked; report recorded in `template_alignment_audit.md` |
| Generated PNG diagram replacement | 0 | Minor underfull warnings only, no fatal errors | Ten TikZ structure/flow diagrams replaced by generated PNG assets; evidence placeholders unchanged; pages 2--13 visually previewed |
| Edge-AI/refocus rewrite | 0 | Minor underfull warnings only, no Overfull after shortening the state-machine row | Hermes compressed to non-safety-critical task assistance; pages 1--16 visually previewed in `final_paper/preview_latest_report_sync/` |
| Hermes task-assistance wording update | 0 | Minor underfull warnings only, no fatal errors or Overfull warnings | `main.tex` and supporting asset notes now align Hermes with accessibility task recording and controlled task execution |
| Evidence update for SD board test and cloud-service tests | 0 | Minor underfull warnings only, no fatal errors or Overfull warnings | Strengthened chapter 3 evidence tables and prose with SD sample serial board-test evidence and cloud-service test counts |
| Sensitivity/haptic/background safety-listening update | 0 | Underfull table wrapping only, no fatal errors or Overfull warnings | `main.tex` now matches latest code status and compiles after two XeLaTeX passes |
| Hardware DOCX merge | 0 | Minor underfull table wrapping only, no fatal errors or Overfull warnings | Added schematic, PCB outline, PCB layout images, and hardware module prose from the user-provided DOCX; rendered pages 8--13 for visual inspection |
| Reviewer-facing terminology pass | 0 | Minor underfull table wrapping only, no fatal errors or Overfull warnings | Rewrote internal implementation terms to 云服务端、本地音频缓存、危险告警、SD 本地样本保存, and regenerated affected diagrams |
| Background safety-listening terminology cleanup | 0 | Minor underfull table wrapping only, no fatal errors or Overfull warnings | Replaced the internal English module label with 后台安全监听 / 低功耗安全监听机制 and removed code-oriented resource wording from report prose |
| P0 reviewer wording cleanup | 0 | Minor underfull table wrapping only, no fatal errors or Overfull warnings | Replaced developer-facing module, serial-port, credential, basic-test, and raw Hermes status wording with reviewer-facing engineering wording |
| Hermes wearable-control positioning update | 0 | Minor underfull table wrapping only, no fatal errors or Overfull warnings | Defined Hermes as a personal AI Agent service and positioned the watch as its wearable interaction/control terminal, while keeping Hermes outside local safety decisions |
| Performance-indicator wording cleanup | 0 | Minor underfull table wrapping only, no fatal errors or Overfull warnings | Reworked section 1.4 as 主要性能与验证指标 and updated continuous stability wording to 24 h without crash, watchdog reset, or memory exhaustion |
| Hermes boundary wording softening | 0 | Minor underfull table wrapping only, no fatal errors or Overfull warnings | Removed repeated wording that explicitly says Hermes does not participate in local safety decisions; retained a natural edge-AI safety throughline and Hermes task-assistance positioning |
| Evidence placeholder compression | 0 | Minor underfull table wrapping only, no fatal errors or Overfull warnings | Replaced scattered photo/screenshot placeholders with consolidated P0 evidence group figures plus P1 optional evidence |
| Deployed model metrics integration | 0 | Minor underfull table wrapping only, no fatal errors or Overfull warnings | Added true model metrics and board timing/resource data; removed internal model iteration wording and alarm from verified model-metric scope |
| Training platform/model structure wording cleanup | 0 | Underfull table wrapping only, no fatal errors, no Overfull warnings, and no cross-reference rerun prompt | Added PC training/evaluation environment, DSCNNTiny input/deployment parameters, and reviewer-facing source-map wording |
| UP template/writing-requirement alignment | 0 | Minor Underfull table wrapping only, no fatal errors, no Undefined control sequence, no Overfull warnings, no undefined references, and no cross-reference rerun prompt | Compressed the abstract to 549 non-whitespace characters, cleaned anonymity/internal wording, formalized chapter 3 evidence figure names, strengthened platform utilization wording, and rebuilt `main.pdf` |
| App supplemental alert alignment | 0 | Minor underfull table wrapping only, no fatal errors, no Undefined control sequence, no Overfull warnings, no undefined references, and no cross-reference rerun prompt | Reframed the safety chain as local risk-state alert plus App/phone remote supplemental alert; added PCB real-photo group placeholder and marked wearable weight/battery/power/endurance as pending real measurements |
| Official heading strict alignment | 0 | Minor underfull table wrapping only, no fatal errors, no Undefined control sequence, no Overfull warnings, no undefined references, and no cross-reference rerun prompt | Removed non-template numbered headings `2.3.3`--`2.3.8`, `3.4`, `3.5`, and `3.5.x`; merged software details under 2.3.2 and all chapter-3 validation/metric evidence under 3.3 特性成果 |
| Formal submission wording cleanup | 0 | Minor underfull table wrapping only, no fatal errors, no Undefined control sequence, no Overfull warnings, no undefined references, and no cross-reference rerun prompt | Replaced agent/process-style phrases with neutral engineering wording for Hermes, App/phone supplemental alert evidence, family/guardian extension, and evidence group guidance |
| 3.3 feature-outcomes rewrite | 0 | Minor underfull table wrapping only, no fatal errors, no Undefined control sequence, no Overfull warnings, no undefined references, and no cross-reference rerun prompt | Rewrote 3.3 特性成果 around core safety chain priority, removed the evidence/materials column, deleted date-specific log prose, and consolidated model/resource metrics into one concise table |

## Compilation

- Engine: XeLaTeX
- Command: `xelatex -interaction=nonstopmode -halt-on-error -file-line-error main.tex`
- Status: success
- PDF: `paper_rewriting_output/final_paper/main.pdf`
- Current PDF length: 27 pages after official-heading strict alignment, UP-template alignment, abstract compression, chapter 3 evidence wording cleanup, platform-utilization strengthening, current deployed model metrics, model structure, training/deployment platform, board resource/timing evidence, App/phone supplemental alert alignment, PCB real-photo placeholder addition, formal submission wording cleanup, and the 3.3 特性成果 rewrite.
- Latest log check: no fatal errors, undefined control sequences, Overfull boxes, undefined references, or cross-reference rerun prompts; only table-related Underfull wrapping warnings remain.
- Latest visual preview: pages 21--26 were rendered after the 3.3 rewrite to `final_paper/preview_3_3_rewrite/`; the software成果 table, new 3.3 feature table, model/resource metrics table, and evidence placeholders show no obvious overlap or margin overflow. Visual evidence placeholders still need real screenshots before submission.

## Content Integrity

- All official template sections present: yes
- Figures present: ten generated PNG structure diagrams, four user-provided hardware design images, plus placeholders for final evidence images
- Tables present: yes
- Citations resolved: reference list is plain numbered text, no BibTeX used yet
- Known author tasks: replace the 6 P0 evidence placeholders with real group figures before submission: full-device photo collage, PCB real-photo collage, danger-alert/haptic collage, sensitivity/background safety-listening screenshot, App/phone notification chain collage, and SD sample-saving collage. P1 additions are one Hermes callback screenshot and a 24 h stability log summary if available. Current deployed model metrics are already present as tables; export a separate model-evaluation image only if a visual figure is desired. The schematic/PCB layout images are design evidence, not a replacement for final real-machine and PCB real-photo evidence. Wearable weight, battery capacity, standby/listening power, and continuous endurance remain pending real measurements.
