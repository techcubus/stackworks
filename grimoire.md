# Grimoire

Shell commands used during development of StackWorks II Pro / pict2ppm / rsrcextract.

---

## Build

```sh
# Main viewer
make

# Resource extractor
make tools/rsrcextract

# PICT renderer
make tools/pict2ppm
```

---

## rsrcextract

Extract every resource from a MacBinary file into a directory tree.
Directories are named by FourCC; files by `<id>` or `<id>_<name>`.

```sh
tools/rsrcextract HyperCard.appl.bin /tmp/hc_rsrc
```

---

## pict2ppm

Render a raw PICT resource to a PPM image file.

```sh
# Basic
tools/pict2ppm /tmp/hc_rsrc/PICT/128_Navigator /tmp/128.ppm

# Verbose (prints opcodes, bitmap dimensions, dst rect)
tools/pict2ppm -v "/tmp/hc_rsrc/PICT/137_HyperCard Box Art" /tmp/137.ppm

# Batch — render all PICT resources, print one line each
for f in /tmp/hc_rsrc/PICT/*; do
    tools/pict2ppm "$f" /tmp/test_$(basename "$f").ppm 2>&1 | tail -1
done
```

---

## Hex inspection

### od

Preferred for byte-level analysis — each byte is its own column, no pair-grouping confusion.

```sh
# Dump first 80 bytes (5 rows × 16 bytes)
od -A x -t x1z <file> | head -5

# Dump rows 5–20 (offsets 0x40–0x13F)
od -A x -t x1z <file> | sed -n '5,20p'

# Dump N rows starting at row R  (row addr = R*16 in hex)
od -A x -t x1z <file> | sed -n '<R>,<R+N>p'

# Skip to a known byte offset, dump 64 bytes
od -A x -t x1z -j <offset> -N 64 <file>
```

Flags:
- `-A x`   — print addresses in hex
- `-t x1`  — output format: hex, 1 byte per unit (no pair-grouping)
- `-t x1z` — same plus ASCII sidebar
- `-j N`   — skip N bytes before output
- `-N N`   — stop after N bytes

Output format:
```
000030 01 03 01 16 00 99 81 16 00 00 00 00 01 03 01 16  >................<
^^^^^^                                                    ^^^^^^^^^^^^^^^^
 addr                                                      ASCII sidebar
```
Row address is the byte offset of the first byte in that row.

---

### xxd

More readable at a glance but groups bytes in pairs — misleading for byte-by-byte analysis.
Use for quick eyeballing or when you want the classic `xxd` look.

```sh
# Dump first 64 bytes
xxd <file> | head -4

# Dump 64 bytes starting at offset 0x40
xxd -s 0x40 -l 64 <file>

# Dump without grouping (one byte per column, like od -t x1)
xxd -c 16 -g 1 <file> | head -5

# Reverse: convert a hex dump back to binary
xxd -r hex.txt > out.bin

# Plain hex dump (no address, no ASCII) — useful for piping
xxd -p <file>
```

Flags:
- `-s N`  — start offset (decimal or 0x hex)
- `-l N`  — length in bytes
- `-c N`  — bytes per row (default 16)
- `-g N`  — group size in bytes (default 2; use 1 to match od -t x1)
- `-p`    — plain hex output, no framing
- `-r`    — reverse: hex dump → binary

Output format (default):
```
00000030: 0103 0116 0099 8116 0000 0000 0103 0116  ................
          ^^^^ ^^^^                                  bytes are PAIRED
```
Bytes are printed in pairs (group of 2), which can misalign byte-offset arithmetic.

---

## ImageMagick

```sh
# Convert PPM to PNG for viewing
convert /tmp/137.ppm /tmp/137.png

# Convert a file whose extension is missing (pict2ppm names output whatever you pass)
convert /tmp/test_137_HyperCard /tmp/137.png
convert /tmp/test_138_Real      /tmp/138.png
```

---

## HyperCard stack inspection

```sh
# Dump all blocks, parts, and field text for a stack
./stackworks -d field_day.hc

# Scan the STAK body for a Pascal string containing the HFS stack name
python3 -c "
data = open('field_day.hc','rb').read()
off = 0
while off + 12 <= len(data):
    bsize = int.from_bytes(data[off:off+4],'big')
    btype = data[off+4:off+8]
    if btype == b'STAK':
        sb = data[off+12:]
        plen = sb[0x234]
        print(f'name plen={plen}')
        if plen: print('  ', sb[0x235:0x235+plen])
        break
    off += bsize
"

# Check block structure of a file (catches non-standard headers like Home.hc)
python3 -c "
data = open('Home.hc','rb').read()
print(f'size: {len(data)}')
off = 0
while off + 12 <= len(data):
    bsize = int.from_bytes(data[off:off+4],'big')
    btype = data[off+4:off+8]
    bid   = int.from_bytes(data[off+8:off+12],'big',signed=True)
    print(f'0x{off:06X}  {btype}  size={bsize}  id={bid}')
    if bsize < 12: break
    off += bsize
"

# Search all .hc files for a Pascal string at a given STAK body offset
python3 -c "
import os
for f in os.listdir('.'):
    if not f.endswith('.hc'): continue
    data = open(f,'rb').read()
    # find STAK block
    off = 0
    while off + 12 <= len(data):
        bsize = int.from_bytes(data[off:off+4],'big')
        if data[off+4:off+8] == b'STAK':
            sb = data[off+12:]
            n = sb[0x234]
            if n and n <= 127: print(f, repr(sb[0x235:0x235+n]))
            else:               print(f, '(no name)')
            break
        if bsize < 12: break
        off += bsize
"
```

---

## Git

```sh
# Check state
git status
git diff tools/pict2ppm.c
git log --oneline -5

# Remove a binary that was accidentally committed
git rm --cached cardviewer

# Stage specific files and commit
git add tools/pict2ppm.c Makefile
git commit -m "message"

# Push current branch
git push
```

---

## GitHub

```sh
# Create a new public repo from the local checkout and push
gh repo create techcubus/stackworks --public --source=. --remote=origin --push
```

---

## MacBinary / resource fork layout (quick reference)

```
MacBinary header: 128 bytes
  [1]      filename length
  [2..64]  filename
  [83..86] data fork length
  [87..90] resource fork length
  [124]    MacBinary version byte
             < 130  → MacBinary I or II, no secondary header
             ≥ 130  → MacBinary III, secondary header length at [120..123]

Data fork starts at:  128 + pad128(sec_hdr_len)
Resource fork starts at: 128 + pad128(sec_hdr_len) + pad128(data_len)

pad128(n) = n ? (n + 127) & ~127 : 0
```

```
Resource fork:
  [0..3]   offset to data section (from fork start)
  [4..7]   offset to map
  [8..11]  data section length
  [12..15] map length

Resource map:
  [24..25] offset from map start to type list
  [26..27] offset from map start to name list

Type list entry (8 bytes):
  [0..3]  FourCC
  [4..5]  (count - 1)
  [6..7]  offset from type list start to reference list

Reference list entry (12 bytes):
  [0..1]  resource ID (int16)
  [2..3]  name offset from name list start (0xFFFF = no name)
  [4]     attributes
  [5..7]  data offset from data section start (uint24, big-endian)
  [8..11] reserved

Resource data block: 4-byte big-endian length, then raw bytes.
```

---

## HyperCard block format (quick reference)

All integers big-endian. Block header is 12 bytes:
```
[0..3]  block size (includes 12-byte header)
[4..7]  FourCC: STAK, CARD, BKGD, BMAP, LIST, PAGE, MAST, STBL, FTBL
[8..11] block ID (int32; STAK always -1)
```
Body starts at byte 12 (offset from body = offset from block − 12).

### STAK body offsets (confirmed empirically)
```
0x18   uint32  background count
0x20   uint32  total card count
0x1AC  uint16  card height (pixels)
0x1AE  uint16  card width (pixels)
0x234  Pascal string  HFS path of stack (length byte + chars)
                      e.g. "HD:.Developing Stacks:Home Regular:Home"
                      last colon-component = stack name
                      zero if stack was never saved with a name
```

### CARD / BKGD body layout
```
0x00   uint32  flags (card) / bkgd id (bkgd)
0x04   uint32  bitmap block ID (0 = no bitmap)
...
0x10   uint16  number of parts
0x12   uint16  last part ID
0x14   uint32  total size of all part records
0x18   uint16  number of content entries
0x1A   uint16  unknown
[part records start here for CARD; BKGD may have a name before parts]
```

### Part record layout (fields and buttons)
Offset from start of this part's record:
```
0x00   uint16  record size (includes this field)
0x02   uint16  part ID
0x04   uint8   type (1=button, 2=field)
0x05   uint8   flags (bit0=visible, bit5=don't wrap, bit6=don't search, bit7=shared text)
0x06   int16   rect.top
0x08   int16   rect.left
0x0A   int16   rect.bottom
0x0C   int16   rect.right
0x0E   uint8   style (high nibble=button style, for fields see below)
0x0F   uint8   field style (0=transparent,1=opaque,2=rect,4=shadow,7=scroll)
0x10   uint16  text_align  (0=left, 1=center, 2=right)
0x12   uint16  unknown flags (0xFFFF = inherit)
0x14   uint16  unknown flags2
0x16   uint16  font_family_id (3=Geneva, 21=Helvetica, 22=Times, 20=Helvetica)
0x18   uint16  text_size (points; 0 = stack default)
0x1A   uint8   text_style  bold=0x01, italic=0x02, underline=0x04,
                            outline=0x08, shadow=0x10, condense=0x20, extend=0x40
0x1B   uint8   line_height
0x1C   null-terminated name string
       [script follows name, also null-terminated]
```

### Content section (follows part records in CARD/BKGD)
```
Entry header (4 bytes):
  uint16  part_id  (negative = background field; positive = card field)
  uint16  entry size (includes the 4-byte header)
Entry body:
  uint8   0x00  (marks this as a text entry, not styled)
  char[]  text  (Mac Roman, \r-delimited lines, not null-terminated)
```

---

## PICT format notes (quick reference)

```
PICT header (10 bytes): picSize(2) + picFrame(rect=8)

Version detection at byte 10:
  0x11 0x01  → PICT v1
  0x00 0x11  → PICT v2 (word 0x0011), followed by:
               word 0x02FF (Version) + word 0x0C00 (HeaderOp) + 24 bytes data

PackBitsRect/PackBitsRgn (0x0098 / 0x0099) opcode data layout:
  rowBytes word  (high bit set = PixMap)
  bounds rect    (8 bytes)
  [if PixMap: pmVersion(2) + packType(2) + packSize(4) + hRes(4) + vRes(4)
               + pixelType(2) + pixelSize(2) + cmpCount(2) + cmpSize(2)
               + planeBytes(4) + pmTable(4) + pmReserved(4)  = 36 bytes]
  [if indexed PixMap (pixelSize ≤ 8): ColorTable]
    ctSeed(4) + ctFlags(2) + ctSize(2) + ctSize+1 entries × {value(2)+R(2)+G(2)+B(2)}
  srcRect (8 bytes)
  dstRect (8 bytes)
  transfer mode (2 bytes)
  [if *Rgn: mask region — rgnSize(2) + (rgnSize-2) bytes]
  compressed row data (PackBits, 1-byte length prefix if rowBytes ≤ 250, else 2-byte)

PackBits decompression:
  n = (int8_t)next_byte
  n == -128        → no-op
  n >= 0           → copy next n+1 bytes literally
  n < 0 (n ≠ -128) → repeat next byte (-n+1) times
```
