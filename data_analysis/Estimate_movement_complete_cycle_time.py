import re
from pathlib import Path


LOG_PATTERN = re.compile(r"ts_ms=(\d+),x=(-?\d+),y=(-?\d+),z=(-?\d+)")
MINIMA_WINDOW_MS = 800
MIN_CYCLE_MS = 1000


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


def load_rows(log_path: Path) -> list[tuple[int, int, int, int]]:
    rows: list[tuple[int, int, int, int]] = []

    for line in log_path.read_text(encoding="utf-8").splitlines():
        match = LOG_PATTERN.search(line)
        if not match:
            continue

        rows.append(tuple(map(int, match.groups())))

    if not rows:
        raise ValueError("No ACCEL samples found in log file.")

    return rows


def select_z_minima(rows: list[tuple[int, int, int, int]]) -> list[tuple[int, int]]:
    zs = [row[3] for row in rows]
    ts = [row[0] for row in rows]

    minima: list[tuple[int, int]] = []
    for index in range(1, len(rows) - 1):
        if zs[index] <= zs[index - 1] and zs[index] <= zs[index + 1]:
            minima.append((ts[index], zs[index]))

    selected: list[list[int]] = []
    for timestamp_ms, z_value in minima:
        if not selected or timestamp_ms - selected[-1][0] > MINIMA_WINDOW_MS:
            selected.append([timestamp_ms, z_value])
        elif z_value < selected[-1][1]:
            selected[-1] = [timestamp_ms, z_value]

    return [(timestamp_ms, z_value) for timestamp_ms, z_value in selected]


def main() -> None:
    log_path = resolve_log_path()
    rows = load_rows(log_path)
    selected = select_z_minima(rows)
    raw_intervals_ms = [current[0] - previous[0] for previous, current in zip(selected, selected[1:])]
    intervals_ms = [interval_ms for interval_ms in raw_intervals_ms if interval_ms >= MIN_CYCLE_MS]

    print(f"log_path: {log_path}")
    print(f"samples: {len(rows)}")
    print("selected_z_minima:")
    for timestamp_ms, z_value in selected:
        print(f"  ts_ms={timestamp_ms}, z={z_value}")

    print("raw_intervals_ms:")
    for interval_ms in raw_intervals_ms:
        print(f"  {interval_ms}")

    print(f"filtered_intervals_ms (>= {MIN_CYCLE_MS}):")
    for interval_ms in intervals_ms:
        print(f"  {interval_ms}")

    if intervals_ms:
        mean_interval_ms = sum(intervals_ms) / len(intervals_ms)
        print(f"mean_interval_ms: {mean_interval_ms:.1f}")
        print(f"mean_interval_s: {mean_interval_ms / 1000:.2f}")
    else:
        print("No complete squat cycles found with current threshold.")


if __name__ == "__main__":
    main()
