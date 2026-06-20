const pieces = {
  K: ["♔", "white", "white king"], Q: ["♕", "white", "white queen"],
  R: ["♖", "white", "white rook"], B: ["♗", "white", "white bishop"],
  N: ["♘", "white", "white knight"], P: ["♙", "white", "white pawn"],
  k: ["♚", "black", "black king"], q: ["♛", "black", "black queen"],
  r: ["♜", "black", "black rook"], b: ["♝", "black", "black bishop"],
  n: ["♞", "black", "black knight"], p: ["♟", "black", "black pawn"],
};

const boardElement = document.querySelector("#board");
const movesElement = document.querySelector("#moves");
const statusElement = document.querySelector("#status");
const engineNoteElement = document.querySelector("#engine-note");
const thinkingElement = document.querySelector("#thinking");
const errorElement = document.querySelector("#error");
const depthElement = document.querySelector("#depth");
const newGameElement = document.querySelector("#new-game");

let game = null;
let selected = null;
let busy = false;

function parseFen(fen) {
  const board = {};
  const rows = fen.split(" ")[0].split("/");
  rows.forEach((row, rowIndex) => {
    let file = 0;
    for (const token of row) {
      if (/\d/.test(token)) {
        file += Number(token);
      } else {
        board[`${"abcdefgh"[file]}${8 - rowIndex}`] = token;
        file += 1;
      }
    }
  });
  return board;
}

function legalDestinations(square) {
  return game.legalMoves.filter((move) => move.startsWith(square)).map((move) => move.slice(2, 4));
}

function renderBoard() {
  const position = parseFen(game.fen);
  const legal = selected ? legalDestinations(selected) : [];
  const lastSquares = game.lastMove ? [game.lastMove.slice(0, 2), game.lastMove.slice(2, 4)] : [];
  boardElement.replaceChildren();

  for (let rank = 8; rank >= 1; rank -= 1) {
    for (let fileIndex = 0; fileIndex < 8; fileIndex += 1) {
      const file = "abcdefgh"[fileIndex];
      const square = `${file}${rank}`;
      const piece = position[square];
      const button = document.createElement("button");
      const isLight = (fileIndex + rank) % 2 === 1;
      button.type = "button";
      button.className = `square ${isLight ? "light" : "dark"}`;
      button.dataset.square = square;
      button.setAttribute("role", "gridcell");
      button.setAttribute("aria-label", piece ? `${square}, ${pieces[piece][2]}` : `${square}, empty`);
      if (square === selected) button.classList.add("selected");
      if (lastSquares.includes(square)) button.classList.add("last");
      if (legal.includes(square)) {
        button.classList.add("legal");
        if (piece) button.classList.add("capture");
      }
      if (piece) {
        const element = document.createElement("span");
        element.className = `piece ${pieces[piece][1]}`;
        element.textContent = pieces[piece][0];
        button.append(element);
      }
      if (fileIndex === 0) button.append(coordinate(rank, "rank"));
      if (rank === 1) button.append(coordinate(file, "file"));
      button.addEventListener("click", () => selectSquare(square, piece));
      boardElement.append(button);
    }
  }
}

function coordinate(value, type) {
  const element = document.createElement("span");
  element.className = `coord ${type}`;
  element.textContent = value;
  return element;
}

function renderHistory() {
  if (game.history.length === 0) {
    movesElement.innerHTML = '<p class="empty-moves">Your game will appear here.</p>';
    return;
  }
  movesElement.replaceChildren();
  for (let index = 0; index < game.history.length; index += 2) {
    const row = document.createElement("div");
    row.className = "move-row";
    row.innerHTML = `<span class="move-number">${index / 2 + 1}.</span><span>${game.history[index]}</span><span>${game.history[index + 1] ?? "…"}</span>`;
    movesElement.append(row);
  }
  movesElement.scrollTop = movesElement.scrollHeight;
}

function render() {
  renderBoard();
  renderHistory();
  statusElement.innerHTML = `<i></i>${game.status}`;
  engineNoteElement.textContent = game.engineNote;
  thinkingElement.hidden = !busy;
  boardElement.setAttribute("aria-busy", String(busy));
}

function selectSquare(square, piece) {
  if (busy || game.gameOver) return;
  const destinations = selected ? legalDestinations(selected) : [];
  if (selected && destinations.includes(square)) {
    playMove(`${selected}${square}`);
    return;
  }
  selected = piece && pieces[piece][1] === "white" && legalDestinations(square).length ? square : null;
  renderBoard();
}

async function request(path, options = {}) {
  const response = await fetch(path, {
    headers: { "Content-Type": "application/json" },
    ...options,
  });
  const payload = await response.json();
  if (!response.ok) throw new Error(payload.error || "Something went wrong");
  return payload;
}

async function playMove(move) {
  busy = true;
  selected = null;
  errorElement.hidden = true;
  render();
  try {
    game = await request("/api/move", {
      method: "POST",
      body: JSON.stringify({ move, depth: Number(depthElement.value) }),
    });
  } catch (error) {
    errorElement.textContent = error.message;
    errorElement.hidden = false;
  } finally {
    busy = false;
    render();
  }
}

async function newGame() {
  busy = true;
  selected = null;
  errorElement.hidden = true;
  if (game) render();
  try {
    game = await request("/api/new", { method: "POST", body: "{}" });
  } catch (error) {
    errorElement.textContent = error.message;
    errorElement.hidden = false;
  } finally {
    busy = false;
    if (game) render();
  }
}

newGameElement.addEventListener("click", newGame);

try {
  game = await request("/api/state");
  render();
} catch (error) {
  document.body.innerHTML = `<main class="fatal"><h1>Could not start the board</h1><p>${error.message}</p></main>`;
}
