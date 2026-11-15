"""Read and write text files without changing their line endings.

Python's default text mode turns every "\\n" into "\\r\\n" on Windows when it
writes. A script that rewrites tracked files in place with the default mode
converts LF files to CRLF, and every line then differs from upstream.
Scripts that rewrite files in place use these functions instead.
"""


def read_exact(path, errors="strict"):
    """Return the file's text with its line endings as stored."""
    with open(path, encoding="utf-8", errors=errors, newline="") as f:
        return f.read()


def write_exact(path, text):
    """Write text as given; no line-ending translation."""
    with open(path, "w", encoding="utf-8", newline="") as f:
        f.write(text)


def match_newlines(text, original):
    """Return LF-only text converted to CRLF when the original used CRLF."""
    return text.replace("\n", "\r\n") if "\r\n" in original else text
