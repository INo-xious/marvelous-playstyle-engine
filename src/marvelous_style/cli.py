from __future__ import annotations

import argparse
from pathlib import Path

from .analysis import analyze
from .chesscom import ChessComClient
from .dataset import build_dataset
from .evaluate import evaluate_style_book


DEFAULT_USERNAME = "MarveIous"
DEFAULT_AGENT = "marvelous-style-engine/0.1 (contact: im.marvel.harisson@gmail.com)"


def parser() -> argparse.ArgumentParser:
    root = argparse.ArgumentParser(description="Build personalization data for MarveIous")
    commands = root.add_subparsers(dest="command", required=True)

    download = commands.add_parser("download", help="Download public Chess.com games")
    download.add_argument("--username", default=DEFAULT_USERNAME)
    download.add_argument("--out", type=Path, default=Path("data/raw"))

    build = commands.add_parser("build", help="Build decision dataset and style book")
    build.add_argument("--username", default=DEFAULT_USERNAME)
    build.add_argument("--pgn", type=Path, required=True)
    build.add_argument("--out", type=Path, default=Path("data/processed"))

    report = commands.add_parser("report", help="Generate descriptive style report")
    report.add_argument("--username", default=DEFAULT_USERNAME)
    report.add_argument("--dataset", type=Path, default=Path("data/processed/decisions.jsonl"))
    report.add_argument("--out", type=Path, default=Path("reports"))

    evaluate = commands.add_parser("evaluate", help="Evaluate the repeated-position style baseline")
    evaluate.add_argument("--dataset", type=Path, default=Path("data/processed/decisions.jsonl"))
    evaluate.add_argument("--style-book", type=Path, default=Path("data/processed/style_book.json"))
    evaluate.add_argument("--out", type=Path, default=Path("reports/baseline_metrics.json"))

    all_command = commands.add_parser("all", help="Download, build, and report")
    all_command.add_argument("--username", default=DEFAULT_USERNAME)
    all_command.add_argument("--root", type=Path, default=Path("."))
    return root


def main() -> None:
    args = parser().parse_args()
    client = ChessComClient(DEFAULT_AGENT)

    if args.command == "download":
        result = client.download_all(args.username, args.out)
        print(f"downloaded {result.game_count} games from {result.archive_count} archives")
        print(result.pgn_path)
        return

    if args.command == "build":
        result = build_dataset(args.pgn, args.username, args.out)
        print(f"built {result.decisions} decisions from {result.games} games")
        print(result.dataset_path)
        return

    if args.command == "report":
        _, markdown = analyze(args.dataset, args.out, args.username)
        print(markdown)
        return

    if args.command == "evaluate":
        metrics = evaluate_style_book(args.dataset, args.style_book, args.out)
        print(metrics)
        return

    raw_dir = args.root / "data" / "raw"
    processed_dir = args.root / "data" / "processed"
    reports_dir = args.root / "reports"
    download_result = client.download_all(args.username, raw_dir)
    build_result = build_dataset(download_result.pgn_path, args.username, processed_dir)
    _, report_path = analyze(build_result.dataset_path, reports_dir, args.username)
    evaluate_style_book(
        build_result.dataset_path,
        build_result.style_book_path,
        reports_dir / "baseline_metrics.json",
    )
    print(
        f"complete: {download_result.game_count} games, "
        f"{build_result.decisions} personal decisions, report at {report_path}"
    )


if __name__ == "__main__":
    main()
