#!/usr/bin/env python3
"""Parse batch config JSON for benchmark scripts.

Modes:
- --has-np <path>: print 1 if any config entry has key 'np', else 0
- --emit-specs <path>: print pipe-delimited spec rows in this order:
  algo, cutset, sketch, hybrid, hybrid_threshold, hybrid_threshold_multiplier,
    batch_size, num_tiers, speed_interval
"""

from __future__ import annotations

import argparse
import json
import sys
from typing import Any


def _load_configs(path: str) -> list[dict[str, Any]]:
    try:
        with open(path, "r", encoding="utf-8") as f:
            data = json.load(f)
    except Exception as exc:
        print(f"Error: failed to parse JSON '{path}': {exc}", file=sys.stderr)
        raise SystemExit(2)

    configs = data.get("configs", []) if isinstance(data, dict) else []
    if not isinstance(configs, list):
        print(f"Error: .configs must be an array in {path}", file=sys.stderr)
        raise SystemExit(2)

    normalized: list[dict[str, Any]] = []
    for cfg in configs:
        normalized.append(cfg if isinstance(cfg, dict) else {})
    return normalized


def _as_str(cfg: dict[str, Any], key: str, default: str = "") -> str:
    if key not in cfg:
        return default
    value = cfg.get(key)
    return "" if value is None else str(value)


def main() -> int:
    parser = argparse.ArgumentParser(add_help=False)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--has-np", dest="has_np_path")
    group.add_argument("--emit-specs", dest="emit_specs_path")
    args = parser.parse_args()

    path = args.has_np_path or args.emit_specs_path
    if path is None:
        return 2

    configs = _load_configs(path)

    if args.has_np_path is not None:
        print("1" if any("np" in cfg for cfg in configs) else "0")
        return 0

    for cfg in configs:
        algo = _as_str(cfg, "algo", "mpi")
        cutset = _as_str(cfg, "cutset", "lct")
        sketch = _as_str(cfg, "sketch", "resizeable")
        hybrid = "true" if bool(cfg.get("hybrid", False)) else "false"
        threshold = _as_str(cfg, "hybrid_threshold")
        threshold_mult = _as_str(cfg, "hybrid_threshold_multiplier")
        batch_size = _as_str(cfg, "batch_size")
        num_tiers = _as_str(cfg, "num_tiers")
        speed_interval = _as_str(cfg, "speed_interval")

        print("|".join([
            algo,
            cutset,
            sketch,
            hybrid,
            threshold,
            threshold_mult,
            batch_size,
            num_tiers,
            speed_interval,
        ]))

    return 0


if __name__ == "__main__":
    raise SystemExit(main())