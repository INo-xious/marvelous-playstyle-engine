from __future__ import annotations

import json
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import requests


@dataclass(frozen=True)
class DownloadResult:
    username: str
    archive_count: int
    game_count: int
    pgn_path: Path
    metadata_path: Path


class ChessComClient:
    """Small serial client for Chess.com's read-only Published Data API."""

    def __init__(self, user_agent: str, delay_seconds: float = 0.15) -> None:
        self.session = requests.Session()
        self.session.headers.update({"User-Agent": user_agent})
        self.delay_seconds = delay_seconds

    def _get_json(self, url: str) -> dict[str, Any]:
        response = self.session.get(url, timeout=30)
        response.raise_for_status()
        return response.json()

    def archive_urls(self, username: str) -> list[str]:
        url = f"https://api.chess.com/pub/player/{username}/games/archives"
        data = self._get_json(url)
        return list(data.get("archives", []))

    def download_all(self, username: str, output_dir: Path) -> DownloadResult:
        output_dir.mkdir(parents=True, exist_ok=True)
        archives = self.archive_urls(username)
        games: list[dict[str, Any]] = []

        for index, url in enumerate(archives):
            payload = self._get_json(url)
            games.extend(payload.get("games", []))
            if index + 1 < len(archives):
                time.sleep(self.delay_seconds)

        pgns = [game["pgn"] for game in games if game.get("pgn")]
        pgn_path = output_dir / f"{username}.pgn"
        metadata_path = output_dir / f"{username}.games.json"
        pgn_path.write_text("\n\n".join(pgns) + ("\n" if pgns else ""), encoding="utf-8")
        metadata_path.write_text(json.dumps(games, indent=2), encoding="utf-8")

        return DownloadResult(
            username=username,
            archive_count=len(archives),
            game_count=len(pgns),
            pgn_path=pgn_path,
            metadata_path=metadata_path,
        )
