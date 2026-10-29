"""Downloads free (CC0) multi-sampled drum kits into the app's kits folder.

    python tools/fetch_kits.py                 # every kit, 6 velocity layers per pad
    python tools/fetch_kits.py --kit swirly --layers 8
    python tools/fetch_kits.py --list

Files are fetched one by one from the sfzinstruments GitHub repositories, pinned to a commit,
and saved as <pad>_v<N>.<ext> (softest first), the layout the app loads. Kits are CC0 (public
domain): nothing is redistributed by this repository, the script downloads them on your
machine. The destination is Documents/Virtual Drum/Kits, or $VDRUM_KITS_DIR, or --dest.
"""

from __future__ import annotations

import argparse
import os
import sys
import urllib.parse
import urllib.request
from dataclasses import dataclass
from pathlib import Path

PADS = ("hihat", "snare", "hightom", "midtom", "floortom", "crash", "ride")


@dataclass(frozen=True)
class PadSource:
    template: str   # path in the repository, {v} = velocity layer number
    layer_count: int


@dataclass(frozen=True)
class KitSource:
    name: str        # folder name in the app
    repo: str
    commit: str
    licence: str
    credit: str
    pads: dict

    def url(self, path: str) -> str:
        return f"https://raw.githubusercontent.com/{self.repo}/{self.commit}/{urllib.parse.quote(path)}"


# Paths checked against the pinned commits (2026-10-29).
KITS = {
    "swirly": KitSource(
        name="Swirly (Karoryfer, CC0)",
        repo="sfzinstruments/karoryfer.swirly-drums",
        commit="c40dafe0011cb2e54c0c220ff0fa308a11fc60f5",
        licence="CC0 1.0 Universal (public domain)",
        credit="Swirly Drums by Karoryfer Samples, https://github.com/sfzinstruments/karoryfer.swirly-drums",
        pads={
            "hihat": PadSource("Samples/hat_closed/hh_closed_vl{v}_rr1.wav", 12),
            "snare": PadSource("Samples/snare_main/snare_hit_vl{v}_rr1_top.wav", 12),
            "hightom": PadSource("Samples/tom_mhi/tom_mhi_vl{v}_rr1.wav", 10),
            "midtom": PadSource("Samples/tom_xmed/tom_xmed_vl{v}_rr1.wav", 10),
            "floortom": PadSource("Samples/tom_floor/tom_floor_vl{v}_rr1.wav", 12),
            "crash": PadSource("Samples/crash/crash_vl{v}_rr1.wav", 12),
            "ride": PadSource("Samples/ride/ride_vl{v}_rr1.wav", 15),
        },
    ),
    "bigrusty": KitSource(
        name="Big Rusty (Karoryfer, CC0)",
        repo="sfzinstruments/karoryfer.big-rusty-drums",
        commit="f07ce00df34a46b6b08375be56fe116cf15782bc",
        licence="CC0 1.0 Universal (public domain)",
        credit="Big Rusty Drums by Karoryfer Samples, https://github.com/sfzinstruments/karoryfer.big-rusty-drums",
        pads={
            "hihat": PadSource("Samples/hihat_14/tc/cl/ht_tc_vl{v}_rr1.flac", 8),
            "snare": PadSource("Samples/snare_14/center/top/sn_center_vl{v}_rr1.flac", 10),
            "hightom": PadSource("Samples/tom_14/center/cl/t14_vl{v}_rr1.flac", 6),
            "midtom": PadSource("Samples/tom_15/center/cl/t15_vl{v}_rr1.flac", 7),
            "floortom": PadSource("Samples/tom_18/center/cl/t18_vl{v}_rr1.flac", 8),
            "crash": PadSource("Samples/crash_17/cr/oh/cr_vl{v}_rr1.flac", 5),
            "ride": PadSource("Samples/ride_22/rd/oh/rd_vl{v}_rr1.flac", 10),
        },
    ),
}


def pick_layers(available: int, wanted: int) -> list:
    """Evenly spread layer numbers (1-based) up to the loudest. The softest quarter is skipped:
    those layers are recorded 30-40 dB below the loudest and sound silent on a virtual kit."""
    lowest = 1 + (available - 1) // 4
    span = available - lowest + 1
    if wanted >= span:
        return list(range(lowest, available + 1))
    if wanted == 1:
        return [available]
    return sorted({round(lowest + i * (span - 1) / (wanted - 1)) for i in range(wanted)})


def plan(kit: KitSource, layers: int) -> list:
    """(repository path, local file name) for every file of a kit."""
    files = []
    for pad in PADS:
        source = kit.pads[pad]
        extension = Path(source.template).suffix
        for index, layer in enumerate(pick_layers(source.layer_count, layers), start=1):
            files.append((source.template.format(v=layer), f"{pad}_v{index}{extension}"))
    return files


def documents_folder() -> Path:
    """The user's Documents folder, as the app finds it (it may be moved, e.g. by OneDrive)."""
    if sys.platform == "win32":
        import ctypes
        from ctypes import wintypes

        buffer = ctypes.create_unicode_buffer(wintypes.MAX_PATH)
        csidl_personal = 5  # "My Documents", the folder JUCE's userDocumentsDirectory returns
        if ctypes.windll.shell32.SHGetFolderPathW(None, csidl_personal, None, 0, buffer) == 0:
            return Path(buffer.value)
    return Path.home() / "Documents"


def default_destination() -> Path:
    override = os.environ.get("VDRUM_KITS_DIR")
    if override:
        return Path(override)
    return documents_folder() / "Virtual Drum" / "Kits"


def download(url: str, target: Path) -> None:
    partial = target.with_name(target.name + ".part")
    with urllib.request.urlopen(url, timeout=60) as response, open(partial, "wb") as out:
        while chunk := response.read(1 << 16):
            out.write(chunk)
    partial.replace(target)


def fetch(kit: KitSource, destination: Path, layers: int) -> int:
    folder = destination / kit.name
    folder.mkdir(parents=True, exist_ok=True)
    files = plan(kit, layers)
    for index, (path, local) in enumerate(files, start=1):
        target = folder / local
        if target.exists():
            continue
        print(f"[{index}/{len(files)}] {kit.name}: {local}", flush=True)
        download(kit.url(path), target)
    (folder / "LICENSE.txt").write_text(
        f"{kit.credit}\nLicence: {kit.licence}\nSource commit: {kit.commit}\n"
        "Downloaded by tools/fetch_kits.py (virtual-drum). Only the first round robin of a subset\n"
        "of the velocity layers is kept, renamed <pad>_v<N> (softest first).\n",
        encoding="utf-8")
    return len(files)


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--kit", choices=sorted(KITS), action="append", help="kit to fetch (default: all)")
    parser.add_argument("--layers", type=int, default=6, help="velocity layers per pad (default: 6)")
    parser.add_argument("--dest", type=Path, default=None, help="kits folder (default: the app folder)")
    parser.add_argument("--list", action="store_true", help="list the kits and exit")
    args = parser.parse_args(argv)

    if args.list:
        for key, kit in KITS.items():
            print(f"{key:10s} {kit.name} - {kit.credit}")
        return 0
    if args.layers < 1:
        parser.error("--layers must be at least 1")

    destination = args.dest or default_destination()
    for key in args.kit or list(KITS):
        count = fetch(KITS[key], destination, args.layers)
        print(f"{KITS[key].name}: {count} files in {destination / KITS[key].name}")
    print("In the app: kit list (top right) > Refresh list.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
