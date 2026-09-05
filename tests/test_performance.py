#!/usr/bin/env python3
"""Run deterministic multisheep scheduling scaling checks."""

import subprocess
from pathlib import Path


def main():
    source = Path("src/main.c").read_text()
    assert "g_timeout_add(group.tick_ms, group_tick, &group)" in source
    assert "g_timeout_add(app->tick_ms, on_tick, app)" not in source
    subprocess.run(["/tmp/esheep_test_multisheep"], check=True)
    print("Performance scaling counters passed for 1, 5, and 10 sheep")


if __name__ == "__main__":
    main()
