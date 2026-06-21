from __future__ import annotations

import argparse
import json
import subprocess
import threading
import webbrowser
from dataclasses import dataclass
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any, Protocol

import chess


ROOT = Path(__file__).resolve().parents[2]
WEB_ROOT = ROOT / "web"
ENGINE_PATH = ROOT / "engine" / "build" / "marvelous-engine"
STYLE_BOOK_PATH = ROOT / "data" / "processed" / "style_book.tsv"


class MoveChooser(Protocol):
    def choose(self, fen: str, depth: int) -> tuple[str, str]: ...


@dataclass(frozen=True)
class OpeningLine:
    name: str
    moves: tuple[str, ...]
    engine_color: chess.Color
    black_route: str | None = None


OPENING_LINES = (
    OpeningLine(
        "Evans Gambit",
        ("e2e4", "e7e5", "g1f3", "b8c6", "f1c4", "f8c5", "b2b4"),
        chess.WHITE,
    ),
    OpeningLine(
        "Fried Liver Attack",
        (
            "e2e4", "e7e5", "g1f3", "b8c6", "f1c4", "g8f6", "f3g5",
            "d7d5", "e4d5", "f6d5", "g5f7",
        ),
        chess.WHITE,
    ),
    OpeningLine(
        "Sicilian Defense response",
        ("e2e4", "c7c5", "g1f3", "d7d6", "d2d4", "c5d4", "f3d4"),
        chess.WHITE,
    ),
    OpeningLine(
        "Englund Gambit",
        ("d2d4", "e7e5", "d4e5", "b8c6", "g1f3", "d8e7"),
        chess.BLACK,
    ),
    OpeningLine(
        "Stafford Gambit",
        ("e2e4", "e7e5", "g1f3", "g8f6", "f3e5", "b8c6", "e5c6", "d7c6"),
        chess.BLACK,
        "stafford",
    ),
    OpeningLine(
        "Fishing Pole Trap",
        (
            "e2e4", "e7e5", "g1f3", "b8c6", "f1b5", "g8f6", "e1g1",
            "f6g4", "h2h3", "h7h5", "h3g4", "h5g4",
        ),
        chess.BLACK,
        "fishing_pole",
    ),
)


def position_key(board: chess.Board) -> str:
    return " ".join(board.fen().split()[:4])


class OpeningBook:
    def __init__(self, lines: tuple[OpeningLine, ...] = OPENING_LINES) -> None:
        self._moves: dict[tuple[chess.Color, str, str | None], tuple[str, str]] = {}
        for line in lines:
            board = chess.Board()
            for move_text in line.moves:
                move = chess.Move.from_uci(move_text)
                if move not in board.legal_moves:
                    raise ValueError(f"illegal move {move_text} in {line.name}")
                if board.turn == line.engine_color:
                    key = (line.engine_color, position_key(board), line.black_route)
                    self._moves.setdefault(key, (move_text, line.name))
                board.push(move)

    def choose(
        self,
        board: chess.Board,
        engine_color: chess.Color,
        black_route: str,
    ) -> tuple[str, str] | None:
        key = position_key(board)
        route = black_route if engine_color == chess.BLACK else None
        result = self._moves.get((engine_color, key, route))
        if result is None:
            result = self._moves.get((engine_color, key, None))
        if result is None or chess.Move.from_uci(result[0]) not in board.legal_moves:
            return None
        return result


DEFAULT_OPENING_BOOK = OpeningBook()


class UciEngine:
    def __init__(self, executable: Path = ENGINE_PATH) -> None:
        if not executable.exists():
            raise FileNotFoundError(
                f"engine not found at {executable}; run 'make engine' first"
            )
        self._process = subprocess.Popen(
            [str(executable)],
            cwd=ROOT,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1,
        )
        self._lock = threading.Lock()
        self._send("uci")
        self._read_until("uciok")
        if STYLE_BOOK_PATH.exists():
            self._send(f"setoption name StyleBookPath value {STYLE_BOOK_PATH}")
            self._read_until_prefix("info string style book")
        self._send("isready")
        self._read_until("readyok")

    def _send(self, command: str) -> None:
        if self._process.stdin is None:
            raise RuntimeError("engine input is unavailable")
        self._process.stdin.write(command + "\n")
        self._process.stdin.flush()

    def _read_line(self) -> str:
        if self._process.stdout is None:
            raise RuntimeError("engine output is unavailable")
        line = self._process.stdout.readline()
        if not line:
            raise RuntimeError("engine stopped unexpectedly")
        return line.strip()

    def _read_until(self, expected: str) -> None:
        while self._read_line() != expected:
            pass

    def _read_until_prefix(self, prefix: str) -> str:
        while True:
            line = self._read_line()
            if line.startswith(prefix):
                return line

    def choose(self, fen: str, depth: int) -> tuple[str, str]:
        with self._lock:
            self._send(f"position fen {fen}")
            self._send(f"go depth {depth}")
            mode = f"Search depth {depth}"
            while True:
                line = self._read_line()
                if line == "info string personal style-book match":
                    mode = "Personal style match"
                if line.startswith("bestmove "):
                    return line.split(maxsplit=1)[1], mode

    def close(self) -> None:
        if self._process.poll() is None:
            try:
                self._send("quit")
                self._process.wait(timeout=2)
            except (BrokenPipeError, subprocess.TimeoutExpired):
                self._process.kill()


class ChessGame:
    def __init__(
        self,
        engine: MoveChooser,
        opening_book: OpeningBook | None = DEFAULT_OPENING_BOOK,
    ) -> None:
        self.engine = engine
        self.opening_book = opening_book
        self.board = chess.Board()
        self.player_color = chess.WHITE
        self.history: list[str] = []
        self.last_move: str | None = None
        self.engine_note = "Ready for your first move"
        self._black_route_index = 0
        self._lock = threading.Lock()

    def reset(self, player_color: str = "white", depth: int = 4) -> dict[str, Any]:
        with self._lock:
            colors = {"white": chess.WHITE, "black": chess.BLACK}
            if player_color not in colors:
                raise ValueError("player color must be white or black")
            self.board.reset()
            self.player_color = colors[player_color]
            self.history.clear()
            self.last_move = None
            self.engine_note = "Ready for your first move"
            if self.player_color == chess.WHITE:
                self._black_route_index = (self._black_route_index + 1) % 2
            else:
                self._play_engine_move(depth)
            return self._state()

    def play(self, move_text: str, depth: int) -> dict[str, Any]:
        with self._lock:
            if self.board.is_game_over():
                raise ValueError("the game is already over")
            if self.board.turn != self.player_color:
                raise ValueError("wait for the engine to move")

            move = self._parse_user_move(move_text)
            self.history.append(self.board.san(move))
            self.board.push(move)
            self.last_move = move.uci()

            if not self.board.is_game_over():
                self._play_engine_move(depth)

            return self._state()

    def state(self) -> dict[str, Any]:
        with self._lock:
            return self._state()

    def _parse_user_move(self, move_text: str) -> chess.Move:
        candidates = [
            move
            for move in self.board.legal_moves
            if move.uci() == move_text
            or (len(move_text) == 4 and move.uci().startswith(move_text))
        ]
        if not candidates:
            raise ValueError(f"illegal move: {move_text}")
        return next(
            (move for move in candidates if move.promotion == chess.QUEEN),
            candidates[0],
        )

    def _play_engine_move(self, depth: int) -> None:
        engine_color = not self.player_color
        black_route = ("stafford", "fishing_pole")[self._black_route_index]
        book_move = (
            self.opening_book.choose(self.board, engine_color, black_route)
            if self.opening_book is not None
            else None
        )
        if book_move:
            engine_move_text, opening_name = book_move
            self.engine_note = f"Opening book: {opening_name}"
        else:
            engine_move_text, self.engine_note = self.engine.choose(
                self.board.fen(), max(1, min(depth, 6))
            )
        engine_move = chess.Move.from_uci(engine_move_text)
        if engine_move not in self.board.legal_moves:
            raise RuntimeError(f"engine returned illegal move {engine_move_text}")
        self.history.append(self.board.san(engine_move))
        self.board.push(engine_move)
        self.last_move = engine_move.uci()

    def _state(self) -> dict[str, Any]:
        outcome = self.board.outcome()
        if outcome:
            if outcome.winner == self.player_color:
                status = "Checkmate - you win"
            elif outcome.winner is not None:
                status = "Checkmate - engine wins"
            else:
                status = f"Draw - {outcome.termination.name.replace('_', ' ').title()}"
        elif self.board.is_check():
            status = "Your turn - check"
        else:
            status = "Your turn"

        return {
            "fen": self.board.fen(),
            "legalMoves": [move.uci() for move in self.board.legal_moves]
            if self.board.turn == self.player_color and not outcome
            else [],
            "history": self.history,
            "lastMove": self.last_move,
            "engineNote": self.engine_note,
            "status": status,
            "gameOver": outcome is not None,
            "playerColor": "white" if self.player_color == chess.WHITE else "black",
        }


def make_handler(game: ChessGame) -> type[BaseHTTPRequestHandler]:
    class Handler(BaseHTTPRequestHandler):
        def do_GET(self) -> None:
            if self.path == "/api/state":
                self._json(game.state())
                return
            self._serve_static()

        def do_POST(self) -> None:
            try:
                payload = self._read_json()
                if self.path == "/api/new":
                    self._json(
                        game.reset(
                            str(payload.get("playerColor", "white")),
                            int(payload.get("depth", 4)),
                        )
                    )
                elif self.path == "/api/move":
                    self._json(
                        game.play(str(payload.get("move", "")), int(payload.get("depth", 4)))
                    )
                else:
                    self._json({"error": "not found"}, HTTPStatus.NOT_FOUND)
            except (ValueError, TypeError, json.JSONDecodeError) as error:
                self._json({"error": str(error)}, HTTPStatus.BAD_REQUEST)
            except Exception as error:
                self._json({"error": str(error)}, HTTPStatus.INTERNAL_SERVER_ERROR)

        def _read_json(self) -> dict[str, Any]:
            length = min(int(self.headers.get("Content-Length", "0")), 4096)
            if length == 0:
                return {}
            return json.loads(self.rfile.read(length))

        def _json(self, payload: dict[str, Any], status: HTTPStatus = HTTPStatus.OK) -> None:
            body = json.dumps(payload).encode()
            self.send_response(status)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def _serve_static(self) -> None:
            requested = "index.html" if self.path in ("/", "") else self.path.lstrip("/")
            path = (WEB_ROOT / requested).resolve()
            if WEB_ROOT not in path.parents or not path.is_file():
                self.send_error(HTTPStatus.NOT_FOUND)
                return
            content_types = {
                ".html": "text/html; charset=utf-8",
                ".css": "text/css; charset=utf-8",
                ".js": "text/javascript; charset=utf-8",
            }
            body = path.read_bytes()
            self.send_response(HTTPStatus.OK)
            self.send_header("Content-Type", content_types.get(path.suffix, "application/octet-stream"))
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, format: str, *args: object) -> None:
            return

    return Handler


def main() -> None:
    parser = argparse.ArgumentParser(description="Run the MarveIous chess GUI")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--no-open", action="store_true")
    args = parser.parse_args()

    engine = UciEngine()
    game = ChessGame(engine)
    server = ThreadingHTTPServer((args.host, args.port), make_handler(game))
    url = f"http://{args.host}:{args.port}"
    print(f"MarveIous GUI running at {url}")
    if not args.no_open:
        threading.Timer(0.4, lambda: webbrowser.open(url)).start()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
        engine.close()


if __name__ == "__main__":
    main()
