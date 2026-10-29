from pathlib import Path

import fetch_kits


def test_pick_layers_spreads_from_softest_to_loudest():
    assert fetch_kits.pick_layers(12, 6) == [3, 5, 7, 8, 10, 12]
    assert fetch_kits.pick_layers(5, 6) == [2, 3, 4, 5]
    assert fetch_kits.pick_layers(10, 1) == [10]
    assert fetch_kits.pick_layers(10, 2) == [3, 10]


def test_every_kit_covers_every_pad_with_app_file_names():
    for kit in fetch_kits.KITS.values():
        files = fetch_kits.plan(kit, 6)
        names = {local for _, local in files}
        for pad in fetch_kits.PADS:
            assert f"{pad}_v1{Path(kit.pads[pad].template).suffix}" in names
        assert all("{v}" not in path for path, _ in files)
        assert len(kit.commit) == 40  # pinned to a commit, not a branch


def test_urls_are_quoted_and_pinned():
    kit = fetch_kits.KITS["swirly"]
    url = kit.url("Samples/a b.wav")
    assert url == f"https://raw.githubusercontent.com/{kit.repo}/{kit.commit}/Samples/a%20b.wav"


def test_fetch_skips_existing_files_and_writes_the_licence(tmp_path, monkeypatch):
    calls = []

    def fake_download(url, target):
        calls.append(url)
        target.write_bytes(b"x")

    monkeypatch.setattr(fetch_kits, "download", fake_download)
    kit = fetch_kits.KITS["bigrusty"]
    assert fetch_kits.fetch(kit, tmp_path, 2) == len(calls) == 14
    fetch_kits.fetch(kit, tmp_path, 2)
    assert len(calls) == 14
    assert "CC0" in (tmp_path / kit.name / "LICENSE.txt").read_text(encoding="utf-8")
