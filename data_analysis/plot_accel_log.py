from __future__ import annotations

import re
from pathlib import Path

import matplotlib.pyplot as plt
import pandas as pd


ACCEL_LOG_PATTERN = re.compile(
    r"ACCEL:\s+ts_ms=(?P<ts_ms>\d+),x=(?P<x>-?\d+),y=(?P<y>-?\d+),z=(?P<z>-?\d+)"
)
ONE_G_LSB = 16384


def parse_accel_log(log_path: Path) -> list[dict[str, int]]:
    records: list[dict[str, int]] = []

    with log_path.open("r", encoding="utf-8") as log_file:
        for line in log_file:
            match = ACCEL_LOG_PATTERN.search(line)
            if not match:
                continue

            records.append(
                {
                    "ts_ms": int(match.group("ts_ms")),
                    "x": int(match.group("x")),
                    "y": int(match.group("y")),
                    "z": int(match.group("z")),
                }
            )

    return records


def build_dataframe(records: list[dict[str, int]]) -> pd.DataFrame:
    if not records:
        raise ValueError("No ACCEL samples found in log file.")

    dataframe = pd.DataFrame.from_records(records, columns=["ts_ms", "x", "y", "z"])
    dataframe["t_s"] = dataframe["ts_ms"] / 1000.0
    return dataframe[["ts_ms", "t_s", "x", "y", "z"]]


def resolve_log_path() -> Path:
    script_dir = Path(__file__).resolve().parent
    project_root = script_dir.parent
    candidates = [
        script_dir / "深蹲.log",
        project_root / "深蹲.log",
    ]

    for candidate in candidates:
        if candidate.exists():
            return candidate

    tried_paths = "\n".join(str(path) for path in candidates)
    raise FileNotFoundError(f"Could not find 深蹲.log. Tried:\n{tried_paths}")


def plot_accel_xyz(dataframe: pd.DataFrame, output_path: Path) -> None:
    output_path.parent.mkdir(parents=True, exist_ok=True)

    axis_specs = [
        ("x", "Accel X", "#d1495b"),
        ("y", "Accel Y", "#2e86ab"),
        ("z", "Accel Z", "#3f9c35"),
    ]
    figure, axes = plt.subplots(3, 1, figsize=(12, 9), sharex=True)
    data_abs_max = max(
        dataframe["x"].abs().max(),
        dataframe["y"].abs().max(),
        dataframe["z"].abs().max(),
    )
    y_limit = int(max(data_abs_max, ONE_G_LSB) * 1.05)

    for axis, (column, title, color) in zip(axes, axis_specs):
        axis.plot(dataframe["t_s"], dataframe[column], color=color, linewidth=1.8)
        axis.axhline(ONE_G_LSB, color="#666666", linestyle="--", linewidth=1, label="+1g")
        axis.axhline(-ONE_G_LSB, color="#999999", linestyle="--", linewidth=1, label="-1g")
        axis.set_ylim(-y_limit, y_limit)
        axis.set_title(title)
        axis.set_ylabel("Raw Accel")
        axis.grid(True, linestyle="--", alpha=0.35)
        axis.legend(loc="upper right")

    axes[-1].set_xlabel("Time (s)")
    figure.suptitle("Accelerometer Axes Aligned by Time", fontsize=14)

    figure.tight_layout(rect=(0, 0, 1, 0.98))
    figure.savefig(output_path, dpi=150)
    plt.show()


def main() -> None:
    script_dir = Path(__file__).resolve().parent
    log_path = resolve_log_path()
    output_path = script_dir / "output" / "accel_aligned.png"

    records = parse_accel_log(log_path)
    dataframe = build_dataframe(records)
    plot_accel_xyz(dataframe, output_path)

    print(f"Parsed {len(dataframe)} ACCEL samples from {log_path}")
    print(f"Saved plot to {output_path}")


if __name__ == "__main__":
    main()
