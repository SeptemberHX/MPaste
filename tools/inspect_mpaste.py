#!/usr/bin/env python3
"""Inspect the contents of a .mpaste file without needing to build the C++ tool."""
import struct
import sys
import os

def read_uint32(f):
    d = f.read(4)
    if len(d) < 4:
        raise EOFError
    return struct.unpack('>I', d)[0]

def read_qstring(f):
    length = read_uint32(f)
    if length == 0xffffffff:
        return None
    data = f.read(length)
    return data.decode('utf-16-be', errors='replace')

def read_qbytearray(f):
    length = read_uint32(f)
    if length == 0xffffffff:
        return None
    return f.read(length)

def skip_qimage(f):
    """Skip a QDataStream-serialized QImage and return a summary string."""
    start = f.tell()
    w = struct.unpack('>i', f.read(4))[0]
    h = struct.unpack('>i', f.read(4))[0]
    if w <= 0 or h <= 0:
        return f"null (at 0x{start:x})"
    depth = struct.unpack('>i', f.read(4))[0]
    _color_count = struct.unpack('>i', f.read(4))[0]
    fmt = struct.unpack('>i', f.read(4))[0]
    bytes_per_line = struct.unpack('>i', f.read(4))[0]
    pixel_data_size = bytes_per_line * h
    f.seek(pixel_data_size, 1)
    return f"{w}x{h} depth={depth} fmt={fmt} pixels={pixel_data_size} bytes (at 0x{start:x})"

def inspect(path):
    with open(path, 'rb') as f:
        data = f.read()
    size = len(data)
    print(f"File: {path}")
    print(f"Size: {size:,} bytes")
    print()

    import io
    f = io.BytesIO(data)

    magic = read_qstring(f)
    version = read_uint32(f)
    flags = read_uint32(f)
    print(f"Magic: {magic}  Version: {version}  Flags: {flags}")

    # QDateTime: julianDay(qint64) + msOfDay(qint32) + timeSpec(quint8)
    julian = struct.unpack('>q', f.read(8))[0]
    ms_of_day = struct.unpack('>i', f.read(4))[0]
    time_spec = struct.unpack('>B', f.read(1))[0]
    if time_spec == 2:  # OffsetFromUTC
        f.read(4)
    elif time_spec == 3:  # TimeZone
        read_qbytearray(f)
    print(f"Time: julianDay={julian} msOfDay={ms_of_day} timeSpec={time_spec}")

    name = read_qstring(f)
    print(f"Name: {name}")

    icon_info = skip_qimage(f)
    print(f"Icon: {icon_info}")

    favicon_info = skip_qimage(f)
    print(f"Favicon: {favicon_info}")

    title = read_qstring(f)
    print(f"Title: {title[:120] if title else '(null)'}")

    url = read_qstring(f)
    print(f"URL: {url[:120] if url else '(null)'}")

    if version >= 5:
        alias = read_qstring(f)
        print(f"Alias: {alias[:120] if alias else '(null)'}")
        if version >= 6:
            pinned = struct.unpack('>?', f.read(1))[0]
            print(f"Pinned: {pinned}")

    content_type = read_uint32(f)
    type_names = {0:'All', 1:'Text', 2:'RichText', 3:'Link', 4:'Color', 5:'Image', 6:'File', 7:'Office'}
    print(f"ContentType: {content_type} ({type_names.get(content_type, 'Unknown')})")

    normalized_text = read_qstring(f)
    if normalized_text:
        display = normalized_text[:200].replace('\n', '\\n')
        print(f"NormalizedText ({len(normalized_text)} chars): {display}")
    else:
        print("NormalizedText: (null)")

    url_count = read_uint32(f)
    print(f"NormalizedUrls: {url_count}")
    for i in range(url_count):
        u = read_qstring(f)
        print(f"  [{i}] {u[:120] if u else '(null)'}")

    fingerprint = read_qbytearray(f)
    print(f"Fingerprint: {fingerprint.hex()[:24] if fingerprint else '(null)'}...")

    thumbnail_info = skip_qimage(f)
    print(f"Thumbnail: {thumbnail_info}")

    mime_offset = struct.unpack('>Q', f.read(8))[0]
    print(f"MimeDataOffset: 0x{mime_offset:x} ({mime_offset:,})")
    print()

    # Parse MIME data
    f.seek(mime_offset)
    fmt_count = read_uint32(f)
    print(f"MIME Formats: {fmt_count}")
    total_payload = 0
    for i in range(fmt_count):
        fmt_name = read_qstring(f)
        payload = read_qbytearray(f)
        pl = len(payload) if payload else 0
        total_payload += pl
        print(f"  [{i:2d}] {fmt_name} => {pl:,} bytes")
    print(f"  Total MIME payload: {total_payload:,} bytes")
    print(f"  File position after MIME: 0x{f.tell():x} ({f.tell():,})")

    # Look for embedded content
    print()
    png_sig = b'\x89PNG\r\n\x1a\n'
    idx = 0
    pngs = []
    while True:
        idx = data.find(png_sig, idx)
        if idx < 0: break
        iend = data.find(b'IEND', idx)
        png_size = (iend + 8 - idx) if iend >= 0 else -1
        pngs.append((idx, png_size))
        idx += 1
    if pngs:
        print(f"Embedded PNGs: {len(pngs)}")
        for i, (p, s) in enumerate(pngs):
            print(f"  PNG #{i} at 0x{p:x}: {s:,} bytes" if s > 0 else f"  PNG #{i} at 0x{p:x}: unknown size")

    svg_idx = data.find(b'<svg')
    if svg_idx >= 0:
        svg_end = data.find(b'</svg>', svg_idx)
        if svg_end >= 0:
            print(f"Embedded SVG at 0x{svg_idx:x}: {svg_end + 6 - svg_idx:,} bytes")

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <file.mpaste>")
        sys.exit(1)
    for path in sys.argv[1:]:
        inspect(path)
        print()
