# HFS manipulation with hfsutils

## Mount / unmount

```sh
hmount "Saved HD.data"   # mount the image (Mini vMac .data file)
humount                  # unmount current volume
```

Mini vMac disk images consist of two files:
- `Saved HD.data`      — the actual disk content (mount this)
- `Saved HD.dirtychunks` — dirty-block bitmap (not needed for read)

## List files

```sh
hls                      # list root
hls -l                   # long listing
hls ":HyperCard:"        # list a folder (Mac path separator = colon)
hls -l ":HyperCard:"
```

Long listing columns: `type/creator  resource_fork_size  data_fork_size  name`

For HyperCard stacks the **data fork** holds the basic stack content - use -r
for a raw data fork copy. Resource fork handling is being thought about.

## Copy files out

```sh
# Copy only data fork (this is where the basic stack data is)
hcopy -r ":HyperCard:Practice" practice.hc

# Copy w/resource fork — MacBinary II
hcopy -m ":HyperCard:Practice" practice_test.hc

# Copy w/resource fork - BinHex
hcopy -b ":HyperCard:MacDungeon Master" .
```

## Navigate (persistent session state)

```sh
hcd ":HyperCard:"        # change directory on the HFS volume
hpwd                     # print current HFS working directory
```

## Other useful commands

```sh
hvol                     # show mounted volume name and stats
hdir                     # directory listing (alternative to hls -l)
hformat -l "MyDisk" /dev/sdX   # format a device as HFS (destructive!)
```

## Path syntax

- Paths use `:` as separator: `":Folder:Subfolder:File"`
- A leading `:` means relative to current HFS directory
- No leading `:` means absolute from volume root
- Spaces in names are fine inside quotes

## Gotchas

- `hmount` operates on the raw image file, not a loop device — no `sudo`
  needed.
- Only one volume mounted at a time per `hmount` session.
- `hls -l` shows resource fork size first, data fork size second.
