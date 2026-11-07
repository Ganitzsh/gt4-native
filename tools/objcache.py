"""A machine-wide, content-addressed cache of the game's objects.

tools/game_cc.py keeps each object it compiled and assembled under the SHA-256 of everything that decides its
bytes (ObjectCache.key). A release build of commits that change a handful of sources then compiles only those:
every other object comes from the cache. (nm's listings are not kept: nm over the objects in chunks takes about
a minute, and reading 127,000 kept listings one file each took longer.) Each entry is written to a temporary file
and renamed into place, so a reader sees a whole entry or none, and any number of checkouts can use the cache at
once; two writers of one key write the same bytes.

The folder: GT4_OBJ_CACHE, else %LOCALAPPDATA%/gt4-native/objcache (~/.cache/gt4-native/objcache elsewhere).
GT4_OBJ_CACHE=0 (or --no-cache) builds without it. Kept: objects that compiled and assembled, and the sources the
compiler refused (their census record and error output: the same inputs fail the same way). A compile killed
for its time or an assembler failure is not kept: tried again each time.
"""
import hashlib
import json
import os
import uuid

FORMAT = b"gt4-objcache-2"   # a change of the entries' meaning: a new number, so old entries are never read


def folder():
    """The cache folder, or None when it is turned off."""
    value = os.environ.get("GT4_OBJ_CACHE")
    if value == "0":
        return None
    if value:
        return os.path.abspath(value)
    base = os.environ.get("LOCALAPPDATA") or os.path.join(os.path.expanduser("~"), ".cache")
    return os.path.join(base, "gt4-native", "objcache")


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def digest(*parts):
    """SHA-256 of the parts (bytes, str, or lists of them), each length-prefixed so no two lists collide."""
    h = hashlib.sha256(FORMAT)

    def add(p):
        if isinstance(p, (list, tuple)):
            h.update(b"L%d:" % len(p))
            for x in p:
                add(x)
            return
        b = p if isinstance(p, bytes) else str(p).encode()
        h.update(b"%d:" % len(b))
        h.update(b)
    for p in parts:
        add(p)
    return h.hexdigest()


def _write(path, data):
    """data into path atomically (temp then rename); an existing entry is left as it is."""
    if os.path.exists(path):
        return
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = f"{path}.{os.getpid()}.{uuid.uuid4().hex[:8]}.tmp"
    with open(tmp, "wb") as f:
        f.write(data)
    try:
        os.replace(tmp, path)
    except OSError:   # (another writer's same entry in place and open: keep it)
        try:
            os.remove(tmp)
        except OSError:
            pass


def _read(path):
    try:
        with open(path, "rb") as f:
            return f.read()
    except OSError:
        return None


class Store:
    """Entries kind/<2 hex>/<key>.<ext> under the cache folder."""

    def __init__(self, root):
        self.root = root

    def path(self, kind, key, ext):
        return os.path.join(self.root, kind, key[:2], f"{key}.{ext}")

    def get(self, kind, key, ext):
        return _read(self.path(kind, key, ext))

    def put(self, kind, key, ext, data):
        _write(self.path(kind, key, ext), data)


class ObjectCache:
    """Objects of tools/game_cc.py, one file per entry, <key>.entry: 4 bytes (little-endian) giving the length of a
    JSON header {"sha256", "record"[, "err"]}, the header, then the object's bytes (none for a refused source). One
    file, not two: on this machine an open costs about 2 ms (a file scan per open), and a warm build opens one per
    source. get() may run on several threads at once; put() on one."""

    def __init__(self, root, base):
        self.store = Store(root)
        self.base = base   # digest of the compiler, assembler, tools, include tree and export policy
        self.stored = 0

    def key(self, rel, source, flags, address):
        return digest(self.base, rel, source, flags, "" if address is None else f"{address:08X}")

    @staticmethod
    def _entry(header, data=b""):
        h = json.dumps(header, sort_keys=True).encode()
        return len(h).to_bytes(4, "little") + h + data

    def get(self, key, o_path, err_path):
        """The census record of a kept result, None when not kept. An object is written to o_path; a source the
        compiler refused has its error output written to err_path instead (and no object)."""
        raw = self.store.get("obj", key, "entry")
        if raw is None or len(raw) < 4:
            return None
        n = int.from_bytes(raw[:4], "little")
        try:
            entry = json.loads(raw[4:4 + n])
        except ValueError:
            return None
        data = raw[4 + n:]
        if entry.get("sha256"):
            if hashlib.sha256(data).hexdigest() != entry["sha256"]:
                return None
            os.makedirs(os.path.dirname(o_path), exist_ok=True)
            with open(o_path, "wb") as f:
                f.write(data)
        else:
            os.makedirs(os.path.dirname(err_path), exist_ok=True)
            with open(err_path, "w", encoding="utf-8", newline="\n") as f:
                f.write(entry.get("err", ""))
        return entry["record"]

    def put(self, key, o_path, record):
        """An object that compiled and assembled."""
        with open(o_path, "rb") as f:
            data = f.read()
        self.store.put("obj", key, "entry", self._entry({"sha256": hashlib.sha256(data).hexdigest(), "record": record}, data))
        self.stored += 1

    def put_refused(self, key, record, err):
        """A source the compiler refused (an error, not a timeout): the same inputs are refused again."""
        self.store.put("obj", key, "entry", self._entry({"sha256": None, "record": record, "err": err}))
        self.stored += 1
