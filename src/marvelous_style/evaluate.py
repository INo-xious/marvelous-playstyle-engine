from __future__ import annotations

import json
from pathlib import Path
from typing import Any

from .analysis import load_decisions


def evaluate_style_book(
    dataset_path: Path,
    style_book_path: Path,
    output_path: Path,
) -> dict[str, Any]:
    decisions = load_decisions(dataset_path)
    style_book = json.loads(style_book_path.read_text(encoding="utf-8"))
    metrics: dict[str, Any] = {}

    for split in ("validation", "test"):
        selected = [decision for decision in decisions if decision["split"] == split]
        covered = [decision for decision in selected if decision["position_key"] in style_book]
        top1 = 0
        top3 = 0
        for decision in covered:
            predicted = [
                item["uci"] for item in style_book[decision["position_key"]]["moves"]
            ]
            top1 += bool(predicted and decision["move_uci"] == predicted[0])
            top3 += decision["move_uci"] in predicted[:3]

        metrics[split] = {
            "decisions": len(selected),
            "covered_decisions": len(covered),
            "coverage_percent": round(100 * len(covered) / len(selected), 2) if selected else 0.0,
            "top1_accuracy_percent": round(100 * top1 / len(covered), 2) if covered else 0.0,
            "top3_accuracy_percent": round(100 * top3 / len(covered), 2) if covered else 0.0,
            "overall_top1_percent": round(100 * top1 / len(selected), 2) if selected else 0.0,
        }

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(metrics, indent=2), encoding="utf-8")
    return metrics
