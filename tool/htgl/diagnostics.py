"""Author-facing diagnostics for the transpiler.

HTGL deliberately supports only a tiny HTML/CSS subset. Historically every
unsupported construct was dropped *silently*, so an author got wrong output with
no feedback (the classic "why is my box black / missing / the wrong size" traps).

A `Diagnostics` collects warnings as the transpiler runs; `cli.main` prints them
to stderr and, with `--strict`, turns them into a non-zero exit. All transpiler
functions take `diag=None` and only record when one is supplied, so passing none
preserves the exact original (silent) behavior.
"""


class Diagnostics:
    def __init__(self):
        self.warnings = []

    def warn(self, message):
        self.warnings.append(message)

    def __bool__(self):
        return bool(self.warnings)

    def __len__(self):
        return len(self.warnings)


def warn(diag, message):
    """Record `message` if `diag` is provided; a no-op otherwise."""
    if diag is not None:
        diag.warn(message)
