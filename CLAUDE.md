# MPaste - Claude Code Guidelines

## Debugging clipboard items

When a bug report mentions a specific clipboard item (e.g. "the first item is loading",
"this rich-text card looks wrong"), **always** start by inspecting the `.mpaste` file
with the existing analysis script before reading source code:

```bash
python tools/inspect_mpaste.py <path-to-file.mpaste>
```

This reveals the item's content type, preview kind, MIME payloads, thumbnail presence,
and other metadata — essential context for diagnosing rendering or preview issues.
