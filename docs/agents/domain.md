# Domain Docs

How the engineering skills should consume this repo's domain documentation when exploring the codebase.

## Canonical context

This repository does not use a separate decision-log tree. The canonical context system lives under `docs/context/`, and stable architecture boundaries live as project knowledge cards.

Before exploring, read:

- `CONTEXT.md`
- `docs/context/INDEX.agent.md`
- `docs/context/knowledge/project/project-profile.md`

For architecture boundaries and long-lived project decisions, read:

- `docs/context/knowledge/project/layering-boundary-map.md`
- `docs/context/knowledge/project/gui-guider-visual-editor-runtime-boundary.md`
- other task-matched `docs/context/knowledge/project/*.md` cards

For plans and current framework work, read:

- `docs/context/plans/`

For previous attempts, board logs, verification evidence, and paths to avoid, read:

- `docs/context/runs/`

## Consumer rules

- Prefer `uv run python scripts/context/validate_context.py --level light --q "<topic>" --brief` before opening raw context files.
- Treat `docs/context/INDEX.agent.md` as the routing entrypoint, not `docs/context/README.md`.
- Treat `docs/context/knowledge/project/` as the source of stable architecture boundaries.
- If output contradicts a stable project knowledge card, surface the conflict explicitly.
- Do not create a parallel decision tree unless the user explicitly asks to reintroduce one.
