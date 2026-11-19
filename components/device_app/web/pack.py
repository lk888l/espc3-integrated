"""Build an offline, single-response control page with its inline script pinned by CSP."""
import base64
import gzip
import hashlib
from pathlib import Path
import sys

source = Path(__file__).parent
output = Path(sys.argv[1])
template = (source / "index.html").read_text(encoding="utf-8")
script = (source / "app.js").read_text(encoding="utf-8")
marker = '<script src="/app.js" defer></script>'
if template.count(marker) != 1 or "</script" in script.lower():
    raise ValueError("Invalid script embedding point in the control page")
page = template.replace(marker, "<script>" + script + "</script>").encode("utf-8")
digest = base64.b64encode(hashlib.sha256(script.encode("utf-8")).digest()).decode("ascii")
policy = ("default-src 'none'; connect-src 'self'; style-src 'unsafe-inline'; "
          f"script-src 'sha256-{digest}'; img-src data:; frame-ancestors 'none'; "
          "base-uri 'none'; form-action 'none'")
(output / "control.html").write_bytes(page)
(output / "control.html.gz").write_bytes(gzip.compress(page, mtime=0))
(output / "control.csp").write_text(policy, encoding="ascii")
