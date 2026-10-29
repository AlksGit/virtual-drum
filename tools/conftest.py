"""Makes tools/ importable from the tests (vdrum package, logger script)."""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
