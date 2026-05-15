# Domain Docs

How the engineering skills should consume this repo's domain documentation when exploring the codebase.

## Canonical context

This repository does not use `docs/adr/` as the main decision directory. The canonical context system lives under `docs/context/`.

Before exploring, read:

- `CONTEXT.md`
- `docs/context/INDEX.agent.md`
- `docs/context/knowledge/project/project-profile.md`

For architecture decisions, read:

- `docs/context/decisions/`

For plans and current framework work, read:

- `docs/context/plans/`

For previous attempts, board logs, verification evidence, and paths to avoid, read:

- `docs/context/runs/`

## Consumer rules

- Prefer `uv run python scripts/context/validate_context.py --level light --q "<topic>" --brief` before opening raw context files.
- Treat `docs/context/INDEX.agent.md` as the routing entrypoint, not `docs/context/README.md`.
- Treat `docs/context/decisions/` as the ADR-equivalent source of architectural decisions.
- If output contradicts a decision under `docs/context/decisions/`, surface the conflict explicitly.
- Do not create a parallel `docs/adr/` decision tree unless the user explicitly asks to migrate the context system.
