import chess
from pathlib import Path

from marvelous_style.gui import ChessGame


class FakeEngine:
    def choose(self, fen: str, depth: int) -> tuple[str, str]:
        board = chess.Board(fen)
        assert chess.Move.from_uci("e7e5") in board.legal_moves
        assert depth == 4
        return "e7e5", "Personal style match"


def test_game_returns_legal_moves_and_engine_reply() -> None:
    game = ChessGame(FakeEngine())
    initial = game.state()
    assert "e2e4" in initial["legalMoves"]

    state = game.play("e2e4", 4)
    assert state["history"] == ["e4", "e5"]
    assert state["lastMove"] == "e7e5"
    assert state["engineNote"] == "Personal style match"
    assert "g1f3" in state["legalMoves"]


def test_game_rejects_illegal_move() -> None:
    game = ChessGame(FakeEngine())
    try:
        game.play("e2e5", 4)
    except ValueError as error:
        assert "illegal move" in str(error)
    else:
        raise AssertionError("expected an illegal move error")


def test_board_grid_keeps_all_eight_ranks_equal() -> None:
    stylesheet = (Path(__file__).parents[1] / "web" / "styles.css").read_text()
    assert "grid-template-rows: repeat(8, minmax(0, 1fr));" in stylesheet
