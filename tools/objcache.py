"""A machine-wide, content-addressed cache of the game's objects and of nm's output on them.

tools/game_cc.py keeps each object it compiled and assembled under the SHA-256 of everything that decides its
bytes (ObjectCache.key); tools/game_link.py keeps nm's output per object under the object's own SHA-256. A
release build of commits that change a handful of sources then compiles only those: every other object, and
nm's listing of it, comes from the cache. Each entry is written to a temporary file and renamed into place, so
a reader sees a whole entry or none, and any number of checkouts can use the cache at once; two writers of one
key write the same bytes.

The folder: GT4_OBJ_CACHE, else %LOCALAPPDATA%/gt4-native/objcache (~/.cache/gt4-native/objcache elsewhere).
GT4_OBJ_CACHE=0 (or --no-cache) builds without it. Only objects that compiled and assembled are kept: a source
that fails is compiled again each time.
"""
import hashlib
import json
import os
import uuid

FORMAT = b"gt4-objcache-1"   # a change of the entries' meaning: a new number, so old entries are never read


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
    """Objects of tools/game_cc.py: <key>.o and <key>.json (its census record), the record written last."""

    def __init__(self, root, base):
        self.store = Store(root)
        self.base = base   # digest of the compiler, assembler, tools, include tree and export policy
        self.hits = 0
        self.stored = 0

    def key(self, rel, source, flags, address):
        return digest(self.base, rel, source, flags, "" if address is None else f"{address:08X}")

    def get(self, key, o_path):
        """The census record of a kept object, with the object written to o_path; None when not kept."""
        record = self.store.get("obj", key, "json")
        data = self.store.get("obj", key, "o") if record is not None else None
        if data is None or hashlib.sha256(data).hexdigest() != json.loads(record)["sha256"]:
            return None
        os.makedirs(os.path.dirname(o_path), exist_ok=True)
        with open(o_path, "wb") as f:
            f.write(data)
        self.hits += 1
        return json.loads(record)["record"]

    def put(self, key, o_path, record):
        with open(o_path, "rb") as f:
            data = f.read()
        self.store.put("obj", key, "o", data)
        self.store.put("obj", key, "json", json.dumps(
            {"sha256": hashlib.sha256(data).hexdigest(), "record": record}, sort_keys=True).encode())
        self.stored += 1


class NmCache:
    """nm's output for one object and one set of nm arguments: the lines it printed for that object, with the
    object's path (nm -A's prefix) taken off."""

    def __init__(self, root, nm_exe):
        self.store = Store(root)
        self.tool = sha256_file(nm_exe)
        self.hits = 0
        self.runs = 0

    def key(self, obj_sha256, args):
        return digest(self.tool, list(args), obj_sha256)

    def get(self, key):
        data = self.store.get("nm", key, "txt")
        if data is None:
            return None
        self.hits += 1
        return data.decode("utf-8").splitlines()

    def put(self, key, lines):
        self.store.put("nm", key, "txt", "".join(l + "\n" for l in lines).encode("utf-8"))
        self.runs += 1
