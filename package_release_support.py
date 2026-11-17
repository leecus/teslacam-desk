"""Shared checksum helper for the Windows release scripts."""
import hashlib
from pathlib import Path


def digest(path: Path) -> str:
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()
