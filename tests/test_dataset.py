from __future__ import annotations

from pathlib import Path

import chess.pgn

from marvelous_style.analysis import analyze
from marvelous_style.dataset import build_dataset, decisions_for_game, deterministic_split
from marvelous_style.evaluate import evaluate_style_book


SAMPLE_PGN = """[Event "Test"]
[Site "https://www.chess.com/game/live/1"]
[Date "2026.01.01"]
[White "MarveIous"]
[Black "Opponent"]
[Result "1-0"]
[WhiteElo "1400"]
[BlackElo "1420"]
[TimeControl "600+5"]
[ECO "C20"]

1. e4 e5 2. Nf3 Nc6 3. Bb5 a6 4. Ba4 Nf6 5. O-O Be7 1-0
"""


def test_game_extracts_only_target_players_decisions(tmp_path: Path) -> None:
    pgn_path = tmp_path / "sample.pgn"
    pgn_path.write_text(SAMPLE_PGN, encoding="utf-8")
    with pgn_path.open() as handle:
        game = chess.pgn.read_game(handle)
    assert game is not None

    decisions = decisions_for_game(game, "marveious", 1)
    assert [decision.move_uci for decision in decisions] == [
        "e2e4",
        "g1f3",
        "f1b5",
        "b5a4",
        "e1g1",
    ]
    assert decisions[-1].is_castling
    assert decisions[0].player_rating == 1400


def test_build_and_report_are_reproducible(tmp_path: Path) -> None:
    pgn_path = tmp_path / "sample.pgn"
    pgn_path.write_text(SAMPLE_PGN, encoding="utf-8")
    result = build_dataset(pgn_path, "MarveIous", tmp_path / "processed")

    assert result.games == 1
    assert result.decisions == 5
    assert result.dataset_path.exists()
    assert result.style_book_path.exists()
    assert result.style_book_tsv_path.exists()

    json_report, markdown_report = analyze(result.dataset_path, tmp_path / "reports", "MarveIous")
    assert json_report.exists()
    assert "Initial Style Report" in markdown_report.read_text(encoding="utf-8")

    metrics = evaluate_style_book(
        result.dataset_path,
        result.style_book_path,
        tmp_path / "reports" / "baseline.json",
    )
    assert set(metrics) == {"validation", "test"}


def test_split_is_stable() -> None:
    assert deterministic_split("game-123") == deterministic_split("game-123")
