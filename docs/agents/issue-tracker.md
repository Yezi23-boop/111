# Issue tracker: GitHub

Issues and PRDs for this repo live as GitHub issues by default. Use the `gh` CLI for issue tracker operations when the user explicitly asks to publish or manage issues.

## Conventions

- **Create an issue**: `gh issue create --title "..." --body "..."`. Use a heredoc for multi-line bodies.
- **Read an issue**: `gh issue view <number> --comments`, also fetching labels when needed.
- **List issues**: `gh issue list --state open --json number,title,body,labels,comments`.
- **Comment on an issue**: `gh issue comment <number> --body "..."`
- **Apply / remove labels**: `gh issue edit <number> --add-label "..."` / `--remove-label "..."`
- **Close**: `gh issue close <number> --comment "..."`

Infer the repo from `git remote -v`; `gh` does this automatically when run inside a clone.

## Current repo note

The repo also has a rich local context system under `docs/context/`. Do not create GitHub issues unless the user asks for issue-tracker output or a skill explicitly needs to publish to the issue tracker.

## When a skill says "publish to the issue tracker"

Create a GitHub issue.

## When a skill says "fetch the relevant ticket"

Run `gh issue view <number> --comments`.
