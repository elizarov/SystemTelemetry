Keep `specifications.txt` in sync with behavior changes before finishing work.
Keep `architecture.txt` in sync with structural/code-organization changes before finishing work.
Document new requirements briefly in the same style as the existing spec.
Always use `build.cmd` for builds.
Keep all build artifacts and temporary compiler files in `build\` so the repository root stays clean.
Ignore the stray `$null` file at the repository root when it appears; it is a Codex sandbox artifact, not a project file.
