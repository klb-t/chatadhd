#!/usr/bin/env python3
"""Linux cold tree backup/restore only. No services, package switching or discovery.
Reject unsupported metadata/links rather than silently weakening preservation.
Manifests and snapshots are private operator material, never product payloads.
"""
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import stat
import sys

class Refusal(Exception):
    pass

def need(condition, code):
    if not condition:
        raise Refusal(code)

def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':')).encode()).hexdigest()

def absolute(path):
    p = Path(os.path.abspath(path))
    need('..' not in Path(path).parts, 'parent_traversal')
    return p

def within(path, root):
    need(path == root or root in path.parents, 'outside_declared_root')

def dirfd(path):
    """Walk every component without following a symlink; no resolve() race."""
    p = absolute(path)
    fd = os.open('/', os.O_PATH | os.O_DIRECTORY | os.O_CLOEXEC)
    try:
        for part in p.parts[1:]:
            nxt = os.open(part, os.O_PATH | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC, dir_fd=fd)
            os.close(fd)
            fd = nxt
        result = os.open('.', os.O_RDONLY | os.O_DIRECTORY | os.O_NOATIME | os.O_CLOEXEC, dir_fd=fd)
        return result
    finally:
        os.close(fd)

def private(fd):
    s = os.fstat(fd)
    need(s.st_uid == os.geteuid() and stat.S_IMODE(s.st_mode) & 0o077 == 0, 'operator_only_parent_required')

def attrs(fd):
    # ENOTSUP/EPERM is an explicit failure, not an invented empty ACL/xattr set.
    return {k: os.getxattr(fd, k) for k in sorted(os.listxattr(fd))}

def metadata(fd, kind):
    s = os.fstat(fd)
    return {'type': kind, 'mode': stat.S_IMODE(s.st_mode), 'uid': s.st_uid, 'gid': s.st_gid,
            'atime_ns': s.st_atime_ns, 'mtime_ns': s.st_mtime_ns,
            'xattrs': {k: hashlib.sha256(v).hexdigest() for k, v in attrs(fd).items()}}

def identity(s):
    return (s.st_dev, s.st_ino, s.st_mode, s.st_uid, s.st_gid, s.st_size, s.st_mtime_ns, s.st_ctime_ns)

def filehash(fd):
    before = os.fstat(fd)
    h = hashlib.sha256()
    offset = 0
    while True:
        block = os.pread(fd, 1024 * 1024, offset)
        if not block:
            break
        h.update(block)
        offset += len(block)
    need(identity(before) == identity(os.fstat(fd)), 'file_changed_during_read')
    return h.hexdigest()

class ColdTree:
    def __init__(self, path):
        self.fd = dirfd(path)
        self.device = os.fstat(self.fd).st_dev
        self.databases = {}

    def close(self):
        for fd in self.databases.values():
            os.close(fd)
        os.close(self.fd)

    def child(self, parent, name):
        s = os.stat(name, dir_fd=parent, follow_symlinks=False)
        need(s.st_dev == self.device, 'external_mount_refused')
        need(stat.S_ISDIR(s.st_mode) or stat.S_ISREG(s.st_mode), 'symlink_or_special_file_refused')
        need(stat.S_ISDIR(s.st_mode) or s.st_nlink == 1, 'hardlink_refused')
        return s

    def regular(self, parent, name, rel):
        s = self.child(parent, name)
        need(stat.S_ISREG(s.st_mode), 'not_regular')
        if rel in self.databases:
            fd = self.databases[rel]
            need((s.st_dev, s.st_ino) == (os.fstat(fd).st_dev, os.fstat(fd).st_ino), 'database_replaced')
            return fd, False
        fd = os.open(name, os.O_RDONLY | os.O_NOFOLLOW | os.O_NOATIME | os.O_CLOEXEC, dir_fd=parent)
        try:
            need(identity(s) == identity(os.fstat(fd)), 'entry_replaced')
            if os.pread(fd, 16, 0) == b'SQLite format 3\x00':
                os.close(fd)
                fd = -1
                writable = os.open(name, os.O_RDWR | os.O_NOFOLLOW | os.O_NOATIME | os.O_CLOEXEC, dir_fd=parent)
                try:
                    need(identity(s) == identity(os.fstat(writable)), 'database_replaced')
                    # Entire byte range, including SQLite's high lock bytes. Never
                    # write the fd or open SQLite on the source. Keep this exact fd
                    # until done: closing another fd for this inode loses POSIX locks.
                    fcntl.lockf(writable, fcntl.LOCK_EX | fcntl.LOCK_NB)
                    self.databases[rel] = writable
                    return writable, False
                except BaseException:
                    os.close(writable)
                    raise
            result = fd
            fd = -1
            return result, True
        finally:
            if fd >= 0:
                os.close(fd)

    def scan(self):
        result = {}
        def walk(fd, rel):
            before = os.fstat(fd)
            result[rel] = metadata(fd, 'dir')
            for name in sorted(os.listdir(fd)):
                key = name if rel == '.' else rel + '/' + name
                s = self.child(fd, name)
                if stat.S_ISDIR(s.st_mode):
                    child = os.open(name, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_NOATIME | os.O_CLOEXEC, dir_fd=fd)
                    try:
                        need(identity(s) == identity(os.fstat(child)), 'directory_replaced')
                        walk(child, key)
                    finally:
                        os.close(child)
                else:
                    child, close = self.regular(fd, name, key)
                    try:
                        record = metadata(child, 'file')
                        record.update(size=os.fstat(child).st_size, sha256=filehash(child))
                        result[key] = record
                    finally:
                        if close:
                            os.close(child)
            need(identity(before) == identity(os.fstat(fd)), 'directory_changed')
        walk(self.fd, '.')
        return result

def apply_metadata(source, target):
    wanted = os.fstat(source)
    got = os.fstat(target)
    if (got.st_uid, got.st_gid) != (wanted.st_uid, wanted.st_gid):
        os.fchown(target, wanted.st_uid, wanted.st_gid)
    os.fchmod(target, stat.S_IMODE(wanted.st_mode))
    values = attrs(source)
    for key in set(os.listxattr(target)) - set(values):
        os.removexattr(target, key)
    for key, value in values.items():
        os.setxattr(target, key, value)
    os.utime(target, ns=(wanted.st_atime_ns, wanted.st_mtime_ns))
    os.fsync(target)
    kind = 'dir' if stat.S_ISDIR(wanted.st_mode) else 'file'
    need(metadata(source, kind) == metadata(target, kind), 'metadata_not_preserved')

def copy_tree(tree, target):
    def walk(src, dst, rel):
        for name in sorted(os.listdir(src)):
            key = name if rel == '.' else rel + '/' + name
            s = tree.child(src, name)
            if stat.S_ISDIR(s.st_mode):
                os.mkdir(name, 0o700, dir_fd=dst)
                sf = os.open(name, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_NOATIME, dir_fd=src)
                df = os.open(name, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_NOATIME, dir_fd=dst)
                try:
                    need(identity(s) == identity(os.fstat(sf)), 'directory_replaced')
                    walk(sf, df, key)
                finally:
                    os.close(sf); os.close(df)
            else:
                sf, close = tree.regular(src, name, key)
                df = os.open(name, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600, dir_fd=dst)
                try:
                    offset = 0
                    while True:
                        block = os.pread(sf, 1024 * 1024, offset)
                        if not block:
                            break
                        offset += len(block)
                        view = memoryview(block)
                        while view:
                            view = view[os.write(df, view):]
                    apply_metadata(sf, df)
                finally:
                    os.close(df)
                    if close:
                        os.close(sf)
        apply_metadata(src, dst)
    walk(tree.fd, target, '.')

def write_json(fd, name, value):
    data = (json.dumps(value, sort_keys=True, indent=2) + '\n').encode()
    dest = os.open(name, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600, dir_fd=fd)
    try:
        view = memoryview(data)
        while view:
            view = view[os.write(dest, view):]
        os.fsync(dest)
    finally:
        os.close(dest)
    os.fsync(fd)
    return hashlib.sha256(data).hexdigest()

def read_json(path, expected=None):
    parent = dirfd(path.parent)
    try:
        private(parent)
        fd = os.open(path.name, os.O_RDONLY | os.O_NOFOLLOW | os.O_NOATIME, dir_fd=parent)
        try:
            private(fd)
            chunks = []
            while True:
                part = os.read(fd, 1024 * 1024)
                if not part: break
                chunks.append(part)
            data = b''.join(chunks)
        finally:
            os.close(fd)
    finally:
        os.close(parent)
    need(expected is None or hashlib.sha256(data).hexdigest() == expected, 'receipt_hash_mismatch')
    return json.loads(data)

def execute(args):
    need(args.writers_stopped, 'explicit_writers_stopped_required')
    allowed = absolute(args.allowed_root)
    if args.action == 'restore':
        snapshot = absolute(args.snapshot)
        within(snapshot, allowed)
        snapshot_fd = dirfd(snapshot)
        try:
            try:
                os.stat('INCOMPLETE.json', dir_fd=snapshot_fd, follow_symlinks=False)
            except FileNotFoundError:
                pass
            else:
                raise Refusal('incomplete_snapshot')
        finally:
            os.close(snapshot_fd)
        complete = read_json(snapshot / 'COMPLETE.json')
        receipt = read_json(snapshot / 'manifest.json', args.manifest_sha256)
        need(receipt['schema'] == 'loom.cold_snapshot/1', 'wrong_snapshot_schema')
        need(complete['manifest_sha256'] == args.manifest_sha256, 'incomplete_snapshot')
        source = snapshot / 'data'
        expected = receipt['tree']
    else:
        source = absolute(args.source)
        expected = None
        if args.action == 'backup':
            receipt = read_json(absolute(args.preflight), args.preflight_sha256)
            need(receipt['source'] == str(source) and receipt['schema'] == 'loom.cold_preflight/1', 'wrong_preflight_source')
            expected = receipt['tree']
    within(source, allowed)
    tree = ColdTree(source)
    try:
        before = tree.scan()
        need(before == tree.scan(), 'unstable_source')
        need(expected is None or before == expected, 'preflight_or_snapshot_changed')
        if args.action == 'preflight':
            target = absolute(args.receipt)
            need(source != target and source not in target.parents, 'receipt_inside_source')
            fd = dirfd(target.parent)
            try:
                private(fd)
                h = write_json(fd, target.name, {'schema':'loom.cold_preflight/1', 'source':str(source), 'tree':before,
                    'sqlite_locked':len(tree.databases), 'requires_all_writers_stopped':True})
            finally:
                os.close(fd)
            return {'ok':True, 'receipt_sha256':h, 'entries':len(before), 'sqlite_locked':len(tree.databases)}
        destination = absolute(args.destination)
        within(destination, absolute(args.destination_root))
        need(source != destination and source not in destination.parents and destination not in source.parents, 'overlapping_paths')
        parent = dirfd(destination.parent)
        try:
            private(parent)
            os.mkdir(destination.name, 0o700, dir_fd=parent)  # EEXIST is always refusal.
            dest = os.open(destination.name, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW, dir_fd=parent)
            try:
                write_json(dest, 'INCOMPLETE.json', {'complete':False})
                os.mkdir('data', 0o700, dir_fd=dest)
                payload = os.open('data', os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW, dir_fd=dest)
                try: copy_tree(tree, payload)
                finally: os.close(payload)
                need(before == tree.scan(), 'source_changed_during_copy')
                copied = ColdTree(destination / 'data')
                try: need(copied.scan() == before, 'copy_or_metadata_mismatch')
                finally: copied.close()
                manifest = {'schema':'loom.cold_snapshot/1', 'tree':before, 'sqlite_locked':len(tree.databases),
                    'metadata':'uid/gid/mode/atime/mtime/xattrs incl ACL verified; ctime/birth time not portable',
                    'consistency':'cold declaration + SQLite locks + before/after hashes; not proof all external writers stopped'}
                h = write_json(dest, 'manifest.json', manifest)
                write_json(dest, 'COMPLETE.json', {'manifest_sha256':h})
                os.unlink('INCOMPLETE.json', dir_fd=dest)
                os.fsync(dest)
            finally:
                os.close(dest)
            os.fsync(parent)
        finally:
            os.close(parent)
        return {'ok':True, 'entries':len(before), 'sqlite_locked':len(tree.databases), 'manifest_sha256':h}
    finally:
        tree.close()

def main():
    os.umask(0o077)
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest='action', required=True)
    for action in ['preflight', 'backup', 'restore']:
        c = sub.add_parser(action)
        c.add_argument('--allowed-root', required=True)
        c.add_argument('--writers-stopped', action='store_true')
        if action == 'restore':
            c.add_argument('--snapshot', required=True)
            c.add_argument('--manifest-sha256', required=True)
        else: c.add_argument('--source', required=True)
        if action == 'preflight': c.add_argument('--receipt', required=True)
        else:
            c.add_argument('--destination', required=True)
            c.add_argument('--destination-root', required=True)
        if action == 'backup':
            c.add_argument('--preflight', required=True)
            c.add_argument('--preflight-sha256', required=True)
    try:
        print(json.dumps(execute(p.parse_args()), sort_keys=True))
        return 0
    except (Refusal, OSError, ValueError, KeyError) as e:
        print(json.dumps({'ok':False, 'error':str(e) if isinstance(e, Refusal) else type(e).__name__,
                          'errno':getattr(e, 'errno', None), 'partial_destination':'never reuse; inspect, no automatic cleanup'}))
        return 2

if __name__ == '__main__':
    sys.exit(main())
