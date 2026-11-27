#!/usr/bin/env python3
"""Add the SHA256 digests that rpm 6 requires to an RPM built by rpmbuild
4.11 (gh-13523).

The RPM file format is:

LEAD       96 bytes: the magic (4 bytes), then legacy fields
SIGNATURE  a section of key-value entries, padded to a multiple of 8 bytes
HEADER     a section of key-value entries
PAYLOAD    compressed cpio data

rpm 6 installs a package only when SHA256 digests cover both HEADER and
PAYLOAD. The digest of PAYLOAD goes into HEADER, where rpmbuild >= 4.14
puts it. SIGNATURE is written anew: its digests cover HEADER, which has
changed, and it gains a SHA256 digest of HEADER. LEAD and PAYLOAD are
copied as they are.

Usage: redigest-rpm.py IN.rpm OUT.rpm (OUT.rpm may be IN.rpm)
"""

import enum
import hashlib
import os
import struct
import sys
import typing

LEAD_SIZE = 96
LEAD_MAGIC = b'\xed\xab\xee\xdb'
SIGNATURE_ALIGNMENT = 8


class EntryTypes(enum.IntEnum):
    """Types of the values this script reads or writes; rpm has more."""

    INT32 = 4
    STRING = 6
    BIN = 7
    STRING_ARRAY = 8


class Keys(enum.IntEnum):
    """Keys of a section (rpm calls them tags), each with the type of its
    value."""

    def __new__(cls, key, type):
        member = int.__new__(cls, key)
        member._value_ = key
        member.type = type
        return member


class SignatureKeys(Keys):
    # The first entry, see Section.
    HEADERSIGNATURES = 62, EntryTypes.BIN
    # Digests of HEADER.
    SHA1HEADER = 269, EntryTypes.STRING
    SHA256HEADER = 273, EntryTypes.STRING
    # The size of HEADER and PAYLOAD, and their MD5 digest.
    SIZE = 1000, EntryTypes.INT32
    MD5 = 1004, EntryTypes.BIN
    # The size of PAYLOAD uncompressed.
    PAYLOADSIZE = 1007, EntryTypes.INT32


# SIGNATURE as rpmbuild 4.11 writes it, besides the first entry. The new
# SIGNATURE replaces these keys and would lose any other, such as a GPG
# signature.
OLD_SIGNATURE_KEYS = {SignatureKeys.SHA1HEADER, SignatureKeys.SIZE,
                      SignatureKeys.MD5, SignatureKeys.PAYLOADSIZE}


class HeaderKeys(Keys):
    # The first entry, see Section.
    HEADERIMMUTABLE = 63, EntryTypes.BIN
    # A digest of PAYLOAD and its algorithm.
    PAYLOADDIGEST = 5092, EntryTypes.STRING_ARRAY
    PAYLOADDIGESTALGO = 5093, EntryTypes.INT32


class DigestAlgos(enum.IntEnum):
    SHA256 = 8


class Entry(typing.NamedTuple):
    """A value's key and type, its offset in the section's data and its
    number of items."""

    key: int
    type: int
    offset: int
    count: int

    FORMAT = struct.Struct('>IIiI')
    SIZE = FORMAT.size

    @classmethod
    def read(cls, in_file):
        return cls(*cls.FORMAT.unpack(in_file.read(cls.SIZE)))

    def encode(self):
        return self.FORMAT.pack(*self)


def padding(offset, alignment):
    """The number of bytes from the offset to a multiple of the
    alignment."""
    return -offset % alignment


class Section:
    """SIGNATURE or HEADER: the entries and the values they point at.

    When rpm installs the package, it keeps HEADER in its database with
    entries of its own appended, yet checks the digests against just the
    package file's entries and data. The first entry and the trailer say
    where those end: the first entry points at the trailer, which ends the
    file's data, and the trailer's offset is -16 times the number of the
    file's entries. read() leaves both out and encode() writes them anew.
    """

    MAGIC = b'\x8e\xad\xe8\x01\x00\x00\x00\x00'

    def __init__(self, entries=(), values=b''):
        self.entries = list(entries)
        self.values = bytearray(values)

    @classmethod
    def read(cls, in_file):
        """Read a section without the first entry and the trailer."""
        offset = in_file.tell()
        if in_file.read(len(cls.MAGIC)) != cls.MAGIC:
            sys.exit('no section magic at offset %d' % offset)
        n_entries, data_size = struct.unpack('>II', in_file.read(8))
        entries = [Entry.read(in_file) for _ in range(n_entries)]
        data = in_file.read(data_size)
        return cls(entries[1:], data[:-Entry.SIZE])

    def __getitem__(self, key):
        assert key.type == EntryTypes.INT32, 'only INT32 values are read'
        entry = next(e for e in self.entries if e.key == key)
        return struct.unpack_from('>I', self.values, entry.offset)[0]

    def add(self, key, value):
        """Append the value, aligned as rpm requires, and an entry pointing
        at it."""
        if key.type == EntryTypes.INT32:
            self.values += bytes(padding(len(self.values), 4))
            encoded, count = struct.pack('>I', value), 1
        elif key.type == EntryTypes.STRING:
            encoded, count = value.encode() + b'\0', 1
        elif key.type == EntryTypes.STRING_ARRAY:
            encoded = b''.join(s.encode() + b'\0' for s in value)
            count = len(value)
        elif key.type == EntryTypes.BIN:
            encoded, count = value, len(value)
        self.entries.append(Entry(key, key.type, len(self.values), count))
        self.values += encoded

    def encode(self, first_key):
        """The section's bytes, with the first entry and the trailer."""
        n_entries = len(self.entries) + 1
        first_entry = Entry(first_key, first_key.type, len(self.values),
                            Entry.SIZE)
        trailer = first_entry._replace(offset=-n_entries * Entry.SIZE)
        data = self.values + trailer.encode()
        return (self.MAGIC + struct.pack('>II', n_entries, len(data)) +
                b''.join(e.encode() for e in [first_entry] + self.entries) +
                data)


def read_payload(in_file, offset):
    """PAYLOAD in chunks: the file from the offset to its end."""
    in_file.seek(offset)
    return iter(lambda: in_file.read(1 << 20), b'')


def main():
    if len(sys.argv) != 3:
        sys.exit('usage: redigest-rpm.py IN.rpm OUT.rpm')
    in_path, out_path = sys.argv[1:]
    with open(in_path, 'rb') as in_file:
        lead = in_file.read(LEAD_SIZE)
        if not lead.startswith(LEAD_MAGIC):
            sys.exit('%s: not an RPM' % in_path)
        old_signature = Section.read(in_file)
        if {e.key for e in old_signature.entries} != OLD_SIGNATURE_KEYS:
            sys.exit('%s: SIGNATURE is not what rpmbuild 4.11 writes'
                     % in_path)
        in_file.seek(padding(in_file.tell(), SIGNATURE_ALIGNMENT), os.SEEK_CUR)
        header = Section.read(in_file)
        payload_offset = in_file.tell()
        stored_payload_size = os.path.getsize(in_path) - payload_offset

        payload_digest = hashlib.sha256()
        for chunk in read_payload(in_file, payload_offset):
            payload_digest.update(chunk)
        header.add(HeaderKeys.PAYLOADDIGEST, [payload_digest.hexdigest()])
        header.add(HeaderKeys.PAYLOADDIGESTALGO, DigestAlgos.SHA256)
        header_bytes = header.encode(HeaderKeys.HEADERIMMUTABLE)

        md5 = hashlib.md5(header_bytes)
        for chunk in read_payload(in_file, payload_offset):
            md5.update(chunk)
        signature = Section()
        signature.add(SignatureKeys.SHA1HEADER,
                      hashlib.sha1(header_bytes).hexdigest())
        signature.add(SignatureKeys.SHA256HEADER,
                      hashlib.sha256(header_bytes).hexdigest())
        signature.add(SignatureKeys.SIZE,
                      len(header_bytes) + stored_payload_size)
        signature.add(SignatureKeys.MD5, md5.digest())
        signature.add(SignatureKeys.PAYLOADSIZE,
                      old_signature[SignatureKeys.PAYLOADSIZE])

        tmp_path = out_path + '.inprogress'
        with open(tmp_path, 'wb') as out_file:
            out_file.write(lead)
            out_file.write(signature.encode(SignatureKeys.HEADERSIGNATURES))
            out_file.write(bytes(padding(out_file.tell(),
                                         SIGNATURE_ALIGNMENT)))
            out_file.write(header_bytes)
            for chunk in read_payload(in_file, payload_offset):
                out_file.write(chunk)
    os.replace(tmp_path, out_path)


if __name__ == '__main__':
    main()
