from __future__ import annotations

import json
from collections import Counter
from pathlib import Path
from typing import Any


def load_decisions(dataset_path: Path) -> list[dict[str, Any]]:
    return [json.loads(line) for line in dataset_path.read_text(encoding="utf-8").splitlines() if line]


def _percentage(count: int, total: int) -> float:
    return round(100.0 * count / total, 2) if total else 0.0


def analyze(dataset_path: Path, output_dir: Path, username: str) -> tuple[Path, Path]:
    output_dir.mkdir(parents=True, exist_ok=True)
    decisions = load_decisions(dataset_path)
    total = len(decisions)
    games = {decision["game_id"] for decision in decisions}
    captures = sum(bool(decision["is_capture"]) for decision in decisions)
    checks = sum(bool(decision["gives_check"]) for decision in decisions)
    castles = [decision["move_uci"] for decision in decisions if decision["is_castling"]]
    colors = Counter(decision["color"] for decision in decisions)
    time_controls = Counter(decision["time_control"] or "unknown" for decision in decisions)
    ecos = Counter(decision["eco"] or "unknown" for decision in decisions)
    ratings = [decision["player_rating"] for decision in decisions if decision["player_rating"]]
    splits = Counter(decision["split"] for decision in decisions)

    report = {
        "username": username,
        "games": len(games),
        "decisions": total,
        "rating": {
            "minimum": min(ratings) if ratings else None,
            "maximum": max(ratings) if ratings else None,
            "average": round(sum(ratings) / len(ratings), 1) if ratings else None,
        },
        "move_tendencies": {
            "capture_rate_percent": _percentage(captures, total),
            "check_rate_percent": _percentage(checks, total),
            "castles": len(castles),
            "kingside_castles": sum(move in {"e1g1", "e8g8"} for move in castles),
            "queenside_castles": sum(move in {"e1c1", "e8c8"} for move in castles),
        },
        "colors": dict(colors),
        "splits": dict(splits),
        "top_time_controls": time_controls.most_common(10),
        "top_eco_codes": ecos.most_common(15),
    }

    json_path = output_dir / "style_report.json"
    markdown_path = output_dir / "style_report.md"
    json_path.write_text(json.dumps(report, indent=2), encoding="utf-8")

    rating = report["rating"]
    tendencies = report["move_tendencies"]
    lines = [
        f"# Initial Style Report: {username}",
        "",
        "This report is descriptive, not yet a trained imitation model.",
        "",
        "## Dataset",
        "",
        f"- Games represented: **{report['games']:,}**",
        f"- Personal move decisions: **{total:,}**",
        f"- Train / validation / test decisions: **{splits['train']:,} / {splits['validation']:,} / {splits['test']:,}**",
        f"- Recorded rating range: **{rating['minimum']} to {rating['maximum']}**",
        f"- Average recorded rating: **{rating['average']}**",
        "",
        "## Move Tendencies",
        "",
        f"- Capture rate: **{tendencies['capture_rate_percent']}%**",
        f"- Checking-move rate: **{tendencies['check_rate_percent']}%**",
        f"- Castles observed: **{tendencies['castles']}**",
        f"- Kingside / queenside castles: **{tendencies['kingside_castles']} / {tendencies['queenside_castles']}**",
        "",
        "## Most Common ECO Codes",
        "",
        "| ECO | Decisions |",
        "| --- | ---: |",
    ]
    lines.extend(f"| {eco} | {count:,} |" for eco, count in report["top_eco_codes"])
    lines.extend(
        [
            "",
            "## Next Modeling Step",
            "",
            "Train a candidate-move reranker on the train split, tune its style weight on validation, and report top-1/top-3 move matching on the untouched test split.",
        ]
    )
    markdown_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return json_path, markdown_path
