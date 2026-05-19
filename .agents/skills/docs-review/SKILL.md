---
name: docs-review
description: "Use only when explicitly invoked: review CaseDash documentation."
---

# Docs Review

## Overview

Use this skill to make repository docs factual, current, and easy to maintain. Apply it to documentation-only reviews and to code changes that require docs updates.

## Workflow

1. Identify the maintained documentation set.
   - Start with repository instructions such as `AGENTS.md`.
   - Read docs indexes, README files, and docs named in the user request.
   - Prefer project-specific single-source rules over general assumptions.

2. Map ownership before editing.
   - Determine which file owns each requirement, behavior, command, format, or example.
   - Treat README as a special user-facing summary and project introduction. It may keep a personal voice and concise high-level repeats for readers, but it should point to maintained docs for details.
   - Replace detailed duplicates with links or short references to the owning file.

3. Review language.
   - Remove changelog-style wording such as `was`, `now`, `previously`, `changed`, `updated`, `new`, `old`, `removed`, `migrated`, and `legacy` when it describes history instead of current behavior.
   - Rewrite into present-tense steady-state statements.
   - Keep history only when the document explicitly exists as a changelog, release note, migration note, or retrospective.

4. Review terminology.
   - Check project-specific terms against `docs/glossary.md`.
   - Prefer canonical glossary terms and context-specific spellings over synonyms.
   - When documentation introduces a new project-specific concept, either add it to the glossary in the same pass or rewrite the text to use an existing term.
   - Preserve exact source identifiers, config tokens, switches, trace prefixes, benchmark names, and user-visible labels when those are the intended subject.

5. Review concision.
   - Prefer short factual sentences.
   - Remove marketing phrasing, embellishment, praise, and implementation trivia that does not help the reader use or maintain the project.
   - Preserve intentional README voice when it helps introduce the project, author, motivation, or contribution path.
   - Keep examples only when they are the maintained source of truth or materially clarify behavior.

6. Review structure.
   - Split long sections when they mix unrelated topics.
   - Convert dense paragraphs into bullets or tables when that improves scanning.
   - Merge tiny repeated sections when a single heading is clearer.
   - Keep headings descriptive and parallel.
   - Keep section headings non-numbered so docs can be reordered without renumbering churn.

7. Edit docs directly.
   - Preserve the repository's existing tone and formatting conventions.
   - Update all affected docs in the same pass so they stay consistent.
   - Avoid broad rewrites when targeted edits solve the issue.

8. Validate.
   - Re-read changed docs for duplicate facts, historical wording, broken references, and awkward structure.
   - Run available docs checks only when the repository defines them or the user requests them.

## AGENTS.md

For project's AGENTS.md file apply special considerations:

- Keep it extremely short to conserve token. Avoid duplications. 
  Don't write like this:
    - Keep `a.md` in sync with A changes before finishing work. 
    - Keep `b.md` in sync with B  changes before finishing work.
  Do: Use the following for reference and in sync before finishing work.
    - `a.md` - A.
    - `b.md` - B.
- Keep only high-impact pitfall notes that are likely to recur. Move rare or detailed concerns to the owning documentation file.

## Architecture

When reviewing a project's `architecture.md`, carefully check its completeness against the source code. Source code is always the source of truth. Keep architecture notes concise and focused on directory-level packages and their subpackages. For actual architectural constraints, also consult the architecture checks in `lint.cmd`; do not run `lint.cmd tidy` unless the user explicitly requests it. Keep the top-level `architecture.md` as a general routing layer that lists major packages and their core responsibilities for easy lookup. When adding a new top-level package that needs detailed architecture notes, put those details in `docs/architecture/<package>.md` and link to it from the top-level file.

## Output

Summarize:

- Files changed.
- Main documentation issues fixed.
- Any docs checks run or skipped.
