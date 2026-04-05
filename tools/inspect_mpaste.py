#!/usr/bin/env python3
"""Inspect the contents of a .mpaste file without needing to build the C++ tool."""
import struct
import sys
import os
import io

def read_uint32(f):
    d = f.read(4)
    if len(d) < 4:
        raise EOFError
    return struct.unpack('>I', d)[0]

def read_int32(f):
    d = f.read(4)
    if len(d) < 4:
        raise EOFError
    return struct.unpack('>i', d)[0]

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

def skip_png_stream(f):
    """Skip a raw PNG stream by parsing its chunk structure.
    Returns (width, height, byte_count)."""
    start = f.tell()
    sig = f.read(8)
    if sig != b'\x89PNG\r\n\x1a\n':
        raise ValueError(f"Bad PNG signature at 0x{start:x}")
    w = h = 0
    while True:
        chunk_len_data = f.read(4)
        if len(chunk_len_data) < 4:
            break
        chunk_len = struct.unpack('>I', chunk_len_data)[0]
        chunk_type = f.read(4)
        if chunk_type == b'IHDR' and chunk_len >= 8:
            w = struct.unpack('>I', f.read(4))[0]
            h = struct.unpack('>I', f.read(4))[0]
            f.seek(chunk_len - 8, 1)  # skip rest of IHDR
        else:
            f.seek(chunk_len, 1)
        f.read(4)  # CRC
        if chunk_type == b'IEND':
            break
    byte_count = f.tell() - start
    return w, h, byte_count

def skip_qpixmap(f):
    """Skip a QDataStream-serialized QPixmap (Qt 6.8 format).

    Qt 6.8 default DataStream version serialises QPixmap as:
      - qint32(0) for null  (4 bytes total)
      - qint32(1) + raw PNG stream for non-null
    Returns a human-readable summary string.
    """
    start = f.tell()
    marker = read_int32(f)
    if marker == 0:
        return f"null (at 0x{start:x})"
    if marker == 1:
        w, h, png_bytes = skip_png_stream(f)
        return f"{w}x{h} PNG {png_bytes:,} bytes (at 0x{start:x})"
    # Unknown marker — might be an older format; try QImage fallback
    f.seek(start)
    return skip_qimage_legacy(f)

def skip_qimage_legacy(f):
    """Skip a legacy QDataStream-serialized QImage (Qt 5 / early Qt 6)."""
    start = f.tell()
    w = read_int32(f)
    h = read_int32(f)
    if w <= 0 or h <= 0:
        return f"null (at 0x{start:x})"
    depth = read_int32(f)
    _color_count = read_int32(f)
    fmt = read_int32(f)
    bytes_per_line = read_int32(f)
    pixel_data_size = bytes_per_line * h
    f.seek(pixel_data_size, 1)
    return f"{w}x{h} depth={depth} fmt={fmt} pixels={pixel_data_size:,} bytes (at 0x{start:x})"

def inspect(path):
    with open(path, 'rb') as fh:
        data = fh.read()
    size = len(data)
    print(f"File: {path}")
    print(f"Size: {size:,} bytes")
    print()

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

    icon_info = skip_qpixmap(f)
    print(f"Icon: {icon_info}")

    favicon_info = skip_qpixmap(f)
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
    type_names = {0:'All', 1:'Text', 2:'Link', 3:'Image', 4:'RichText', 5:'File', 6:'Color', 7:'Office'}
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

    thumbnail_info = skip_qpixmap(f)
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
        label = fmt_name or "(null)"
        if label == "text/html" and payload:
            html_preview = payload.decode('utf-8', errors='replace')[:120].replace('\n', '\\n')
            print(f"  [{i:2d}] {label} => {pl:,} bytes  preview: {html_preview}")
        else:
            print(f"  [{i:2d}] {label} => {pl:,} bytes")
    print(f"  Total MIME payload: {total_payload:,} bytes")
    print(f"  File position after MIME: 0x{f.tell():x} ({f.tell():,})")

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <file.mpaste>")
        sys.exit(1)
    for path in sys.argv[1:]:
        try:
            inspect(path)
        except Exception as e:
            print(f"Error: {e}")
        print()
