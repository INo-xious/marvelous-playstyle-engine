from __future__ import annotations

import hashlib
import json
from collections import Counter, defaultdict
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Iterator

import chess
import chess.pgn


@dataclass(frozen=True)
class Decision:
    game_id: str
    split: str
    ply: int
    color: str
    fen: str
    position_key: str
    move_uci: str
    move_san: str
    clock_seconds: float | None
    legal_move_count: int
    is_capture: bool
    gives_check: bool
    is_castling: bool
    is_en_passant: bool
    promotion: str | None
    time_control: str
    result: str
    player_rating: int | None
    opponent_rating: int | None
    eco: str
    opening_url: str


@dataclass(frozen=True)
class BuildResult:
    games: int
    decisions: int
    train_decisions: int
    validation_decisions: int
    test_decisions: int
    dataset_path: Path
    style_book_path: Path
    style_book_tsv_path: Path


def deterministic_split(game_id: str) -> str:
    bucket = int(hashlib.sha256(game_id.encode("utf-8")).hexdigest()[:8], 16) % 100
    if bucket < 70:
        return "train"
    if bucket < 85:
        return "validation"
    return "test"


def position_key(board: chess.Board) -> str:
    return " ".join(board.fen().split()[:4])


def read_games(pgn_path: Path) -> Iterator[chess.pgn.Game]:
    with pgn_path.open(encoding="utf-8", errors="replace") as handle:
        while game := chess.pgn.read_game(handle):
            yield game


def _rating(headers: chess.pgn.Headers, key: str) -> int | None:
    value = headers.get(key, "")
    return int(value) if value.isdigit() else None


def decisions_for_game(game: chess.pgn.Game, username: str, game_index: int) -> list[Decision]:
    username_lower = username.casefold()
    white = game.headers.get("White", "")
    black = game.headers.get("Black", "")
    if white.casefold() == username_lower:
        player_color = chess.WHITE
        color_name = "white"
        player_rating = _rating(game.headers, "WhiteElo")
        opponent_rating = _rating(game.headers, "BlackElo")
    elif black.casefold() == username_lower:
        player_color = chess.BLACK
        color_name = "black"
        player_rating = _rating(game.headers, "BlackElo")
        opponent_rating = _rating(game.headers, "WhiteElo")
    else:
        return []

    game_id = game.headers.get("Link") or f"game-{game_index}"
    split = deterministic_split(game_id)
    board = game.board()
    node: chess.pgn.GameNode = game
    decisions: list[Decision] = []
    ply = 0

    while node.variations:
        next_node = node.variation(0)
        move = next_node.move
        if move is None:
            break
        ply += 1
        if board.turn == player_color:
            promotion = chess.piece_name(move.promotion) if move.promotion else None
            decisions.append(
                Decision(
                    game_id=game_id,
                    split=split,
                    ply=ply,
                    color=color_name,
                    fen=board.fen(),
                    position_key=position_key(board),
                    move_uci=move.uci(),
                    move_san=board.san(move),
                    clock_seconds=next_node.clock(),
                    legal_move_count=board.legal_moves.count(),
                    is_capture=board.is_capture(move),
                    gives_check=board.gives_check(move),
                    is_castling=board.is_castling(move),
                    is_en_passant=board.is_en_passant(move),
                    promotion=promotion,
                    time_control=game.headers.get("TimeControl", ""),
                    result=game.headers.get("Result", "*"),
                    player_rating=player_rating,
                    opponent_rating=opponent_rating,
                    eco=game.headers.get("ECO", ""),
                    opening_url=game.headers.get("ECOUrl", ""),
                )
            )
        board.push(move)
        node = next_node

    return decisions


def build_dataset(pgn_path: Path, username: str, output_dir: Path) -> BuildResult:
    output_dir.mkdir(parents=True, exist_ok=True)
    dataset_path = output_dir / "decisions.jsonl"
    style_book_path = output_dir / "style_book.json"
    style_book_tsv_path = output_dir / "style_book.tsv"
    all_decisions: list[Decision] = []
    game_count = 0

    for game_count, game in enumerate(read_games(pgn_path), start=1):
        all_decisions.extend(decisions_for_game(game, username, game_count))

    with dataset_path.open("w", encoding="utf-8") as handle:
        for decision in all_decisions:
            handle.write(json.dumps(asdict(decision), sort_keys=True) + "\n")

    position_moves: dict[str, Counter[str]] = defaultdict(Counter)
    for decision in all_decisions:
        if decision.split == "train":
            position_moves[decision.position_key][decision.move_uci] += 1

    style_book = {
        key: {
            "total": sum(moves.values()),
            "moves": [
                {"uci": move, "count": count, "probability": count / sum(moves.values())}
                for move, count in moves.most_common()
            ],
        }
        for key, moves in position_moves.items()
        if sum(moves.values()) >= 2
    }
    style_book_path.write_text(json.dumps(style_book, indent=2), encoding="utf-8")
    tsv_lines = ["position_key\tmove_uci\tprobability\tobservations"]
    for key, entry in style_book.items():
        top = entry["moves"][0]
        tsv_lines.append(
            f"{key}\t{top['uci']}\t{top['probability']:.6f}\t{entry['total']}"
        )
    style_book_tsv_path.write_text("\n".join(tsv_lines) + "\n", encoding="utf-8")

    split_counts = Counter(decision.split for decision in all_decisions)
    return BuildResult(
        games=game_count,
        decisions=len(all_decisions),
        train_decisions=split_counts["train"],
        validation_decisions=split_counts["validation"],
        test_decisions=split_counts["test"],
        dataset_path=dataset_path,
        style_book_path=style_book_path,
        style_book_tsv_path=style_book_tsv_path,
    )
