"""Byte-addressed nonvolatile memory backed by an atomic file replacement."""
import os
from pathlib import Path
import tempfile


class Storage:
    def __init__(self, path=None, size=4096):
        self.path = Path(path) if path is not None else None
        self.size = size
        self.writes = 0
        self.data = bytes([255]) * size
        if self.path is not None and self.path.exists():
            data = self.path.read_bytes()
            if len(data) != size:
                raise ValueError(f"存储器大小错误：{self.path}，需要 {size} 字节")
            self.data = data

    def _check(self, offset, length):
        if type(offset) is not int or type(length) is not int or offset < 0 or length < 0 or offset + length > self.size:
            raise ValueError("存储器访问越界")

    def read(self, offset, length):
        self._check(offset, length)
        return self.data[offset:offset + length]

    def write(self, offset, data):
        data = bytes(data)
        self._check(offset, len(data))
        updated = self.data[:offset] + data + self.data[offset + len(data):]
        if updated == self.data:
            return
        if self.path is not None:
            self.path.parent.mkdir(parents=True, exist_ok=True)
            name = None
            try:
                with tempfile.NamedTemporaryFile(dir=self.path.parent, delete=False) as file:
                    name = file.name
                    file.write(updated)
                    file.flush()
                    os.fsync(file.fileno())
                os.replace(name, self.path)
            finally:
                if name is not None and os.path.exists(name):
                    os.unlink(name)
        self.data = updated
        self.writes += 1

    def erase(self):
        self.write(0, bytes([255]) * self.size)
