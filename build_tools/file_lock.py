"""Process locks shared by source preparation and Conda tool installation."""

from contextlib import contextmanager
import errno
import os
from pathlib import Path
import time


@contextmanager
def file_lock(path: Path, timeout: float = 600):
    """Keep the lock file in place; the OS releases the lock on process exit."""
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("a+b") as stream:
        deadline = time.monotonic() + timeout
        while True:
            try:
                stream.seek(0)
                if os.name == "nt":
                    import msvcrt
                    msvcrt.locking(stream.fileno(), msvcrt.LK_NBLCK, 1)
                else:
                    import fcntl
                    fcntl.flock(stream.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
                break
            except OSError as error:
                if error.errno not in (errno.EACCES, errno.EAGAIN, errno.EDEADLK):
                    raise
                if time.monotonic() >= deadline:
                    raise RuntimeError(f"Timed out waiting for lock: {path}") from error
                time.sleep(0.1)
        try:
            # Initialize only after locking: another process may already hold
            # the first byte. Windows permits locking beyond the end of a file.
            stream.seek(0, os.SEEK_END)
            if stream.tell() == 0:
                stream.write(b"0")
                stream.flush()
            yield
        finally:
            stream.seek(0)
            if os.name == "nt":
                msvcrt.locking(stream.fileno(), msvcrt.LK_UNLCK, 1)
            else:
                fcntl.flock(stream.fileno(), fcntl.LOCK_UN)
