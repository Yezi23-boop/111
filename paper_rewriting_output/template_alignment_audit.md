# Template Alignment Audit

## Template Source

- Official template: `C:\Users\ye\Desktop\2026嵌入式大赛应用赛道作品报告模板(1) (1).docx`
- Current LaTeX source: `paper_rewriting_output/final_paper/main.tex`
- Current PDF: `paper_rewriting_output/final_paper/main.pdf`
- Audit date: 2026-07-04

## Template Structure Check

| Template item | Current report status |
| --- | --- |
| 作品名称 | Present |
| 摘要，800字内 | Present, about 542 chars |
| 第一部分 作品概述 | Present |
| 功能与特性，400字内 | Present, about 274 chars |
| 应用领域，400字内 | Present, about 231 chars |
| 主要技术特点，400字内 | Present, table-based |
| 主要性能指标，200字内，建议表格 | Present, table-based |
| 主要创新点，200字内，逐点给出 | Present, about 189 chars |
| 设计流程，200字内，可加重要图 | Present, about 196 chars |
| 第二部分 系统组成及功能说明 | Present |
| 整体介绍，需系统整体框图 | Present, includes TikZ system block diagram |
| 硬件系统介绍 | Present |
| 2.2.1 硬件整体介绍 | Present, photo placeholder retained |
| 2.2.2 机械设计介绍 | Present, structure placeholder retained |
| 2.2.3 电路各模块介绍 | Present, module table retained |
| 软件系统介绍 | Present |
| 2.3.1 软件整体介绍 | Present, includes software architecture diagram |
| 2.3.2 软件各模块介绍 | Present, module table retained |
| 第三部分 完成情况及性能参数 | Present |
| 整体介绍，正面和斜45度照片 | Present, photo placeholder retained |
| 3.2.1 机械成果 | Present |
| 3.2.2 电路成果 | Present |
| 3.2.3 软件成果 | Present |
| 特性成果，功能和量化指标 | Present |
| 第四部分 总结 | Present |
| 可扩展之处，300字内 | Present, about 290 chars |
| 心得体会，1000字内 | Present, about 503 chars |
| 第五部分 参考文献，20篇以内 | Present, 6 entries |

## Header And Page Setup

- The template DOCX contains `word/media/image1.png`; the LaTeX header image `figures/official_header.png` has the same SHA-256 hash.
- Template page margins extracted from DOCX:
  - top: 2.54 cm
  - bottom: 2.54 cm
  - left: 3.17 cm
  - right: 3.17 cm
  - header distance: 1.50 cm
  - footer distance: 1.75 cm
- `main.tex` now uses the template page margins through `geometry`.

## Content Hygiene

- Template instruction text such as word limits and anonymous-submission reminders is not present in the report body.
- No school name or advisor name was inserted.
- Current report keeps required evidence placeholders for photos and screenshots instead of fabricating evidence.

## Compile Check

- XeLaTeX compile status: success.
- Current PDF length: 20 pages.
- Overfull check after template margin sync: no overfull warnings remained in the final compile log; only narrow-table underfull warnings remain.
- Preview directory after template-margin check: `paper_rewriting_output/final_paper/preview_template_margin_check/`.

## Remaining Author Evidence

- Handheld/watch real photos: missing.
- Danger Alerting UI screenshot: missing.
- Hermes page/reply screenshot: missing.
- Android phone notification screenshot: missing.
- SD card WAV + JSON sample file screenshot: missing.
- Server `/v1/watch/alerts` log screenshot: missing.
