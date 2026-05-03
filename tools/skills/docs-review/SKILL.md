---
name: docs-review
description: Review and edit repository documentation for steady-state language, single-source information, concise factual wording, and clean structure. Use when Codex is asked to review docs, fix documentation quality, remove changelog-style wording, deduplicate docs, or normalize project documentation.
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

4. Review concision.
   - Prefer short factual sentences.
   - Remove marketing phrasing, embellishment, praise, and implementation trivia that does not help the reader use or maintain the project.
   - Preserve intentional README voice when it helps introduce the project, author, motivation, or contribution path.
   - Keep examples only when they are the maintained source of truth or materially clarify behavior.

5. Review structure.
   - Split long sections when they mix unrelated topics.
   - Convert dense paragraphs into bullets or tables when that improves scanning.
   - Merge tiny repeated sections when a single heading is clearer.
   - Keep headings descriptive and parallel.

6. Edit docs directly.
   - Preserve the repository's existing tone and formatting conventions.
   - Update all affected docs in the same pass so they stay consistent.
   - Avoid broad rewrites when targeted edits solve the issue.

7. Validate.
   - Re-read changed docs for duplicate facts, historical wording, broken references, and awkward structure.
   - Run available docs checks only when the repository defines them or the user requests them.

## Output

Summarize:

- Files changed.
- Main documentation issues fixed.
- Any docs checks run or skipped.
