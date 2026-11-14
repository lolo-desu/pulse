"""Copy Explorer's original icon resources without re-encoding their pixels.

Run on Windows with Python 3:
    python tools/extract_explorer_icon.py

The RT_GROUP_ICON directory supplies dimensions and color depth; each RT_ICON
payload already contains its original PNG or DIB, palette, alpha and AND mask.
Do not round-trip these resources through System.Drawing.Icon.Save, which can
write a reduced indexed-color image with an incorrect palette/mask layout.
"""

import argparse
import ctypes
import os
from pathlib import Path
import struct


class ResourceModule:
    def __init__(self, path):
        self.api = ctypes.WinDLL("kernel32", use_last_error=True)
        self.api.LoadLibraryExW.argtypes = [ctypes.c_wchar_p, ctypes.c_void_p, ctypes.c_uint32]
        self.api.LoadLibraryExW.restype = ctypes.c_void_p
        self.api.FindResourceW.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p]
        self.api.FindResourceW.restype = ctypes.c_void_p
        self.api.SizeofResource.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
        self.api.SizeofResource.restype = ctypes.c_uint32
        self.api.LoadResource.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
        self.api.LoadResource.restype = ctypes.c_void_p
        self.api.LockResource.argtypes = [ctypes.c_void_p]
        self.api.LockResource.restype = ctypes.c_void_p
        self.api.FreeLibrary.argtypes = [ctypes.c_void_p]
        self.api.FreeLibrary.restype = ctypes.c_bool
        # LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE: never run code.
        self.handle = self.api.LoadLibraryExW(str(path), None, 0x22)
        if not self.handle:
            raise ctypes.WinError(ctypes.get_last_error())

    def read(self, kind, name):
        resource_name = name if isinstance(name, int) else ctypes.cast(
            ctypes.c_wchar_p(name), ctypes.c_void_p
        )
        resource = self.api.FindResourceW(self.handle, resource_name, kind)
        if not resource:
            raise ctypes.WinError(ctypes.get_last_error())
        size = self.api.SizeofResource(self.handle, resource)
        loaded = self.api.LoadResource(self.handle, resource)
        pointer = self.api.LockResource(loaded) if loaded else None
        if not size or not pointer:
            raise ctypes.WinError(ctypes.get_last_error())
        return ctypes.string_at(pointer, size)

    def close(self):
        self.api.FreeLibrary(self.handle)


def extract_icon(source, group):
    module = ResourceModule(source)
    try:
        directory = module.read(14, group)  # RT_GROUP_ICON
        reserved, kind, count = struct.unpack_from("<HHH", directory)
        if reserved != 0 or kind != 1 or len(directory) != 6 + count * 14:
            raise ValueError("Invalid icon group directory")
        images = []
        for index in range(count):
            entry = struct.unpack_from("<BBBBHHIH", directory, 6 + index * 14)
            width, height, colors, unused, planes, depth, size, resource_id = entry
            payload = module.read(3, resource_id)  # RT_ICON
            if len(payload) != size:
                raise ValueError(f"Resource {resource_id}: unexpected payload length")
            images.append(((width, height, colors, unused, planes, depth), payload))

        # Preserve every source image, including any alternate color depths.
        offset = 6 + count * 16
        entries = []
        for metadata, payload in images:
            entries.append(struct.pack("<BBBBHHII", *metadata, len(payload), offset))
            offset += len(payload)
        icon = struct.pack("<HHH", 0, 1, count) + b"".join(entries)
        icon += b"".join(payload for _, payload in images)
        return icon, images
    finally:
        module.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=Path(os.environ["SystemRoot"]) / "explorer.exe")
    parser.add_argument("--group", default="ICO_MYCOMPUTER", help="Resource name or numeric resource ID")
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parents[1] / "src/app/pulse.ico")
    parser.add_argument("--check", action="store_true", help="Verify the output matches the original resources")
    args = parser.parse_args()
    group = int(args.group) if args.group.isdecimal() else args.group
    icon, images = extract_icon(args.source, group)
    if args.check:
        if args.output.read_bytes() != icon:
            parser.exit(1, "Icon differs from the original resource group\n")
    else:
        args.output.write_bytes(icon)
    for metadata, payload in images:
        width, height, _, _, _, depth = metadata
        print(f"{width or 256}x{height or 256}: {depth} bpp, {len(payload)} bytes")
    print("Verified original resource bytes" if args.check else "Wrote original resource bytes")


if __name__ == "__main__":
    main()
