from pathlib import Path

import chess
import pytest

from marvelous_style.gui import DEFAULT_OPENING_BOOK, ChessGame


class FakeEngine:
    def choose(self, fen: str, depth: int) -> tuple[str, str]:
        board = chess.Board(fen)
        assert chess.Move.from_uci("e7e5") in board.legal_moves
        assert depth == 4
        return "e7e5", "Personal style match"


def test_game_returns_legal_moves_and_engine_reply() -> None:
    game = ChessGame(FakeEngine(), opening_book=None)
    initial = game.state()
    assert "e2e4" in initial["legalMoves"]

    state = game.play("e2e4", 4)
    assert state["history"] == ["e4", "e5"]
    assert state["lastMove"] == "e7e5"
    assert state["engineNote"] == "Personal style match"
    assert "g1f3" in state["legalMoves"]


def test_game_rejects_illegal_move() -> None:
    game = ChessGame(FakeEngine(), opening_book=None)
    try:
        game.play("e2e5", 4)
    except ValueError as error:
        assert "illegal move" in str(error)
    else:
        raise AssertionError("expected an illegal move error")


def test_board_grid_keeps_all_eight_ranks_equal() -> None:
    stylesheet = (Path(__file__).parents[1] / "web" / "styles.css").read_text()
    assert "grid-template-rows: repeat(8, minmax(0, 1fr));" in stylesheet


@pytest.mark.parametrize(
    ("moves", "engine_color", "route", "expected_move", "opening_name"),
    [
        (("e2e4", "e7e5", "g1f3", "b8c6", "f1c4", "f8c5"), chess.WHITE, "stafford", "b2b4", "Evans Gambit"),
        (("d2d4",), chess.BLACK, "stafford", "e7e5", "Englund Gambit"),
        (("e2e4", "e7e5", "g1f3", "g8f6", "f3e5"), chess.BLACK, "stafford", "b8c6", "Stafford Gambit"),
        (("e2e4", "e7e5", "g1f3", "b8c6", "f1c4", "g8f6"), chess.WHITE, "stafford", "f3g5", "Fried Liver Attack"),
        (("e2e4", "e7e5", "g1f3", "b8c6", "f1b5", "g8f6", "e1g1"), chess.BLACK, "fishing_pole", "f6g4", "Fishing Pole Trap"),
        (("e2e4", "c7c5"), chess.WHITE, "stafford", "g1f3", "Sicilian Defense response"),
    ],
)
def test_requested_opening_moves(
    moves: tuple[str, ...],
    engine_color: chess.Color,
    route: str,
    expected_move: str,
    opening_name: str,
) -> None:
    board = chess.Board()
    for move in moves:
        board.push_uci(move)

    assert DEFAULT_OPENING_BOOK.choose(board, engine_color, route) == (
        expected_move,
        opening_name,
    )


def test_black_side_starts_after_engine_white_move() -> None:
    game = ChessGame(FakeEngine())

    state = game.reset("black", 4)

    assert state["playerColor"] == "black"
    assert state["history"] == ["e4"]
    assert state["lastMove"] == "e2e4"
    assert state["engineNote"].startswith("Opening book:")
    assert "e7e5" in state["legalMoves"]


def test_black_player_can_move_and_receive_anti_sicilian_reply() -> None:
    game = ChessGame(FakeEngine())
    game.reset("black", 4)

    state = game.play("c7c5", 4)

    assert state["history"] == ["e4", "c5", "Nf3"]
    assert state["lastMove"] == "g1f3"
    assert state["engineNote"] == "Opening book: Sicilian Defense response"
    assert state["playerColor"] == "black"
    assert "b8c6" in state["legalMoves"]


def test_side_controls_are_present_in_gui() -> None:
    html = (Path(__file__).parents[1] / "web" / "index.html").read_text()
    script = (Path(__file__).parents[1] / "web" / "app.js").read_text()
    assert 'data-side="white"' in html
    assert 'data-side="black"' in html
    assert 'game.playerColor === "black"' in script
