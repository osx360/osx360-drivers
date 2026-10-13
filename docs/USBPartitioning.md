# USB partitioning for OS X Tiger installer

Quick summary
- The Xbox 360 REQUIRES an MBR (msdos) partition table — the console will not recognize GPT.
- Partition 1: FAT32 (primary) — payloads (XeLL, XeLL Launcher, BadUpdate/Abadavatar, OpenBIOS).
- Partition 2: HFS+ — Mac OS X Tiger installer (DVD/ISO restored to the partition).

Recommended sizes
- FAT32: 512 MB–2 GB (1 GB is a good default).
- HFS+: >= 4 GB (the Tiger image used here is ~2.7 GB; allow 4 GB for buffer).

Example layout
```text
USB (MBR)
├─ Partition 1 — FAT32 (primary) — payloads
└─ Partition 2 — HFS+ — Mac OS X Tiger installer
```

Placement & naming
- Only put the following files in the FAT32 partition root: XeLL Reloaded (xell.bin), OpenBIOS, and the mkets.
- Other payloads and supporting files can be placed in subdirectories as needed.
- Format the disk you will install Tiger onto as Mac OS Extended (Journaled). The HFS+ partition on the USB used to carry the installer does not need journaling.

Deploy note
- The Tiger ISO/Disk image must be written byte-for-byte into the HFS+ partition (target the partition, not the whole disk). Double-check device/partition before writing — this is destructive.

Notes
- Abadavatar requires a disk configured as the system disk (internal or external); it will not reliably work without a system disk present.
