#!/usr/bin/env python3
"""
This file is part of PolySRV Desktop,
a Telegram Desktop fork for the PolySRV multi-server client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

Generates the PolySRV icon set.

The artwork is supplied as a square PNG named icon_source.png next to this
script. Every size the platforms ask for is produced from it, together with
the Windows icon and the two macOS icon sets.

Until the artwork exists, the script falls back to a placeholder mark it draws
itself, so that the icon set can always be regenerated.

Usage: python3 generate_icons.py
"""

import math
import os
import struct
import sys
import zlib

# The placeholder mark, used when no artwork has been supplied.
BackdropTop = (0x6B, 0x5C, 0xE7)
BackdropBottom = (0x4B, 0x3E, 0xC9)
Bar = (0xFF, 0xFF, 0xFF)
CornerRadius = 0.225
BarWidth = 0.44
BarHeight = 0.115
BarGap = 0.045
BarRadius = BarHeight / 2

# The artwork, and the files it is turned into.
SourceName = 'icon_source.png'

ArtDirectory = '.'
XcassetsDirectory = os.path.join(
    '..', '..', 'Telegram', 'Images.xcassets', 'Icon.iconset')
AppIconSetDirectory = os.path.join(
    '..', '..', 'Telegram', 'Images.xcassets', 'Icon.appiconset')
AppxAssetsDirectory = os.path.join('..', 'uwp', 'AppX', 'Assets')


def Clamp(value, low, high):
    return low if value < low else (high if value > high else value)


def RoundedRectDistance(px, py, cx, cy, halfWidth, halfHeight, radius):
    qx = abs(px - cx) - (halfWidth - radius)
    qy = abs(py - cy) - (halfHeight - radius)
    return (math.hypot(max(qx, 0), max(qy, 0))
            + min(max(qx, qy), 0)
            - radius)


def Coverage(distance):
    return Clamp(0.5 - distance, 0, 1)


def Blend(base, top, alpha):
    return (
        int(base[0] + (top[0] - base[0]) * alpha),
        int(base[1] + (top[1] - base[1]) * alpha),
        int(base[2] + (top[2] - base[2]) * alpha),
    )


def RenderPlaceholder(size, roundShape=False, backdrop=True):
    """Draws the placeholder mark into a list of RGBA rows."""
    half = size / 2
    radius = CornerRadius * size
    barHalfWidth = BarWidth * size / 2
    barHalfHeight = BarHeight * size / 2
    barRadius = BarRadius * size
    gap = BarGap * size
    stackHeight = 3 * BarHeight * size + 2 * gap
    topOfStack = (size - stackHeight) / 2

    rows = []
    for y in range(size):
        py = y + 0.5
        t = y / max(size - 1, 1)
        gradient = (
            int(BackdropTop[0] + (BackdropBottom[0] - BackdropTop[0]) * t),
            int(BackdropTop[1] + (BackdropBottom[1] - BackdropTop[1]) * t),
            int(BackdropTop[2] + (BackdropBottom[2] - BackdropTop[2]) * t),
        )
        row = bytearray()
        for x in range(size):
            px = x + 0.5
            if backdrop:
                distance = RoundedRectDistance(
                    px, py, half, half, half, half, radius)
                if roundShape:
                    distance = max(
                        distance,
                        math.hypot(px - half, py - half) - half)
                alpha = Coverage(distance)
                colour = gradient
            else:
                alpha = 0
                colour = (0, 0, 0)

            for index in range(3):
                barTop = topOfStack + index * (2 * barHalfHeight + gap)
                barAlpha = Coverage(RoundedRectDistance(
                    px, py,
                    half, barTop + barHalfHeight,
                    barHalfWidth, barHalfHeight, barRadius))
                if barAlpha:
                    colour = Blend(colour, Bar, barAlpha)
                    alpha = max(alpha, barAlpha)

            row += bytes((
                colour[0], colour[1], colour[2], int(alpha * 255 + 0.5),
            ))
        rows.append(bytes(row))
    return rows


def ReadPngSize(path):
    with open(path, 'rb') as file:
        header = file.read(24)
    if header[:8] != b'\x89PNG\r\n\x1a\n':
        return None
    return struct.unpack('>II', header[16:24])


def PngBytes(size, rows):
    raw = b''.join(b'\x00' + row for row in rows)

    def Chunk(kind, data):
        chunk = struct.pack('>I', len(data)) + kind + data
        return chunk + struct.pack('>I', zlib.crc32(kind + data) & 0xFFFFFFFF)

    header = struct.pack('>IIBBBBB', size, size, 8, 6, 0, 0, 0)
    return (b'\x89PNG\r\n\x1a\n'
            + Chunk(b'IHDR', header)
            + Chunk(b'IDAT', zlib.compress(raw, 9))
            + Chunk(b'IEND', b''))


def WritePng(path, size, rows):
    with open(path, 'wb') as file:
        file.write(PngBytes(size, rows))


def WriteIco(path, entries):
    """Writes an .ico holding PNG encoded images."""
    directory = bytearray(struct.pack('<HHH', 0, 1, len(entries)))
    offset = 6 + 16 * len(entries)
    payloads = []
    for size, data in entries:
        directory += struct.pack(
            '<BBBBHHII',
            0 if size >= 256 else size,
            0 if size >= 256 else size,
            0,
            0,
            1,
            32,
            len(data),
            offset)
        offset += len(data)
        payloads.append(data)
    with open(path, 'wb') as file:
        file.write(bytes(directory))
        for data in payloads:
            file.write(data)


def ScaleRows(rows, size, target):
    """Resamples the rows of an image with a box filter."""
    if target == size:
        return rows
    scale = target / size
    out = []
    for y in range(target):
        first = int(y / scale)
        last = max(first + 1, int((y + 1) / scale))
        row = bytearray()
        for x in range(target):
            left = int(x / scale)
            right = max(left + 1, int((x + 1) / scale))
            totals = [0, 0, 0, 0]
            count = 0
            for sy in range(first, min(last, size)):
                source = rows[sy]
                for sx in range(left, min(right, size)):
                    offset = sx * 4
                    totals[0] += source[offset]
                    totals[1] += source[offset + 1]
                    totals[2] += source[offset + 2]
                    totals[3] += source[offset + 3]
                    count += 1
            count = max(count, 1)
            row += bytes((totals[0] // count, totals[1] // count,
                          totals[2] // count, totals[3] // count))
        out.append(bytes(row))
    return out


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    os.chdir(here)

    source = os.path.join(here, SourceName)
    haveArtwork = os.path.exists(source)
    if haveArtwork:
        size = ReadPngSize(source)
        if not size or size[0] != size[1]:
            print('%s must be a square PNG.' % SourceName)
            return 1
        print('using the artwork %s (%d pixels)' % (SourceName, size[0]))
        with open(source, 'rb') as file:
            artwork = file.read()
    else:
        print('no %s found, using the placeholder mark' % SourceName)

    sizes = [16, 32, 48, 64, 96, 128, 256, 512, 1024]
    rendered = {}
    for size in sizes:
        if haveArtwork:
            # The artwork is kept as is; only the placeholder is drawn.
            rendered[size] = None
        else:
            rendered[size] = RenderPlaceholder(size)
        print('prepared', size)

    def Write(directory, name, size):
        target = os.path.join(directory, name)
        os.makedirs(directory, exist_ok=True)
        if haveArtwork:
            with open(target, 'wb') as file:
                file.write(artwork)
        else:
            WritePng(target, size, rendered[size])
        print('wrote', target)

    # The application icon, in the sizes the platforms ask for.
    Write(ArtDirectory, 'icon16.png', 16)
    Write(ArtDirectory, 'icon16@2x.png', 32)
    Write(ArtDirectory, 'icon32.png', 32)
    Write(ArtDirectory, 'icon32@2x.png', 64)
    Write(ArtDirectory, 'icon48.png', 48)
    Write(ArtDirectory, 'icon48@2x.png', 96)
    Write(ArtDirectory, 'icon64.png', 64)
    Write(ArtDirectory, 'icon64@2x.png', 128)
    Write(ArtDirectory, 'icon128.png', 128)
    Write(ArtDirectory, 'icon128@2x.png', 256)
    Write(ArtDirectory, 'icon256.png', 256)
    Write(ArtDirectory, 'icon256@2x.png', 512)
    Write(ArtDirectory, 'icon512.png', 512)
    Write(ArtDirectory, 'icon512@2x.png', 1024)

    # The round variant, used as the macOS round app icon.
    if haveArtwork:
        Write(ArtDirectory, 'icon_round512@2x.png', 1024)
    else:
        Write(ArtDirectory, 'icon_round512@2x.png', 1024,
              RenderPlaceholder(1024, roundShape=True))

    # The Windows icon.
    if haveArtwork:
        entries = [(size, artwork) for size in (16, 32, 48, 64, 128, 256)]
    else:
        entries = [(size, PngBytes(size, rendered[size]))
                   for size in (16, 32, 48, 64, 128, 256)]
    WriteIco(os.path.join(ArtDirectory, 'icon256.ico'), entries)
    print('wrote', os.path.join(ArtDirectory, 'icon256.ico'))

    # The in-application logo: with the backdrop, and without it.
    if haveArtwork:
        Write(ArtDirectory, 'logo_256.png', 256)
        Write(ArtDirectory, 'logo_256_no_margin.png', 256)
    else:
        Write(ArtDirectory, 'logo_256.png', 256)
        Write(ArtDirectory, 'logo_256_no_margin.png', 256,
              RenderPlaceholder(256, backdrop=False))

    # The macOS icon sets.
    Write(XcassetsDirectory, 'icon_16x16.png', 16)
    Write(XcassetsDirectory, 'icon_16x16@2x.png', 32)
    Write(XcassetsDirectory, 'icon_32x32.png', 32)
    Write(XcassetsDirectory, 'icon_32x32@2x.png', 64)
    Write(XcassetsDirectory, 'icon_128x128.png', 128)
    Write(XcassetsDirectory, 'icon_128x128@2x.png', 256)
    Write(XcassetsDirectory, 'icon_256x256.png', 256)
    Write(XcassetsDirectory, 'icon_256x256@2x.png', 512)
    Write(XcassetsDirectory, 'icon_512x512.png', 512)
    Write(XcassetsDirectory, 'icon_512x512@2x.png', 1024)

    Write(AppIconSetDirectory, 'icon16.png', 16)
    Write(AppIconSetDirectory, 'icon16@2x.png', 32)
    Write(AppIconSetDirectory, 'icon32.png', 32)
    Write(AppIconSetDirectory, 'icon32@2x.png', 64)
    Write(AppIconSetDirectory, 'icon128.png', 128)
    Write(AppIconSetDirectory, 'icon128@2x.png', 256)
    Write(AppIconSetDirectory, 'icon256.png', 256)
    Write(AppIconSetDirectory, 'icon256@2x.png', 512)
    Write(AppIconSetDirectory, 'icon512.png', 512)
    Write(AppIconSetDirectory, 'icon512@2x.png', 1024)

    # The AppX logos keep their own sizes, so each is replaced in place.
    for root, _, files in os.walk(AppxAssetsDirectory):
        for name in files:
            if not name.endswith('.png'):
                continue
            path = os.path.join(root, name)
            size = ReadPngSize(path)
            if not size or size[0] != size[1]:
                continue
            Write(root, name, size[0])
    return 0


if __name__ == '__main__':
    sys.exit(main())
