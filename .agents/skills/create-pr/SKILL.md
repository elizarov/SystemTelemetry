---
name: create-pr
description: "Use only when explicitly invoked: create CaseDash pull requests with gh using the repository PR style from AGENTS.md."
---

# Create PR

## Workflow

1. Read `AGENTS.md`, then inspect branch state, remotes, commits, and `git status --short`.
2. Ensure the branch is pushed before creating the pull request.
3. Write the PR title and body in the `AGENTS.md` style:
   - Start the title with an action verb such as `Added`, `Changed`, `Fixed`, `Improved`, `Refactored`, or `Removed`.
   - Use tight factual bullets for non-trivial changes.
   - Describe behavior outcomes, not file-by-file narration.
   - Do not use `## Summary`, `## Testing`, generated checklists, or template headings.
   - Do not mention validation or tests unless they were the explicit reason for the work.
4. Put the body in a temporary file under `build\`, then run `gh pr create --base <base> --head <branch> --title "<title>" --body-file <body-file>`.
5. Report the PR URL and title.
