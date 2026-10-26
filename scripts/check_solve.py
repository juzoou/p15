from dataclasses import dataclass
from pathlib import Path
from typing import Sequence, Tuple

try:
    from .parse_vasekin import _parse_input, InputError, read_input_text
    from .puzzle import PuzzleInput, Board, DIRECTIONS
except ImportError:
    from parse_vasekin import _parse_input, InputError, read_input_text
    from puzzle import PuzzleInput, Board, DIRECTIONS

def _read_puzzle(path: str | Path) -> PuzzleInput:
    return _parse_input(read_input_text(path))

@dataclass(frozen=True)
class CheckResult:
    final_board: Board
    valid_moves: int
    placed_tiles: int
    total_cost: int
    score: int
    solved: bool


def inversion_count(board: Sequence[int]) -> int:
    """Число инверсий за O(n log n) через дерево Фенвика."""
    values = [value for value in board if value != 0]
    tree = [0] * (len(values) + 1)

    def prefix_sum(index: int) -> int:
        total = 0
        while index > 0:
            total += tree[index]
            index -= index & -index
        return total

    inversions = 0
    seen = 0
    for value in values:
        # Номера фишек лежат в диапазоне 1..len(values), индексация дерева с 1.
        not_greater = prefix_sum(value)
        inversions += seen - not_greater
        index = value
        while index < len(tree):
            tree[index] += 1
            index += index & -index
        seen += 1
    return inversions

def is_solvable(puzzle: PuzzleInput) -> bool:
    if puzzle.rows == 1 or puzzle.cols == 1:
        return tuple(tile for tile in puzzle.board if tile) == tuple(range(1, len(puzzle.board)))
    inversions = inversion_count(puzzle.board)
    blank_index = puzzle.board.index(0)
    if puzzle.cols % 2 == 1:
        return inversions % 2 == 0
    blank_row_from_bottom = puzzle.rows - (blank_index // puzzle.cols)
    return (inversions + blank_row_from_bottom) % 2 == 1

def check_if_solve(path: str | Path) -> bool:
    """Return reachability for a task file (not the file's text)."""
    return is_solvable(_read_puzzle(path))

def _check_order(puzzle: PuzzleInput):
    if puzzle.order is None:
        raise InputError("для проверки должен быть задан блок ORDER")
    board = list(puzzle.board)
    rows, cols = puzzle.rows, puzzle.cols
    costs_by_direction = dict(zip(DIRECTIONS, puzzle.costs))
    total_cost = 0
    blank = board.index(0)

    for move_number, (tile, direction) in enumerate(puzzle.order, 1):
        if not 1 <= tile <= puzzle.tile_count:
            raise InputError(
                f"ход {move_number}: номер фишки {tile} вне диапазона "
                f"1..{puzzle.tile_count}"
            )
        # Проверяем, что источник фишки и пустая клетка расположены рядом,
        # а указанное направление движения фишки ведёт ровно в пустую клетку.
        blank_row, blank_col = divmod(blank, cols)
        if direction == "U":
            source = blank + cols
            legal = blank_row + 1 < rows
        elif direction == "D":
            source = blank - cols
            legal = blank_row > 0
        elif direction == "L":
            source = blank + 1
            legal = blank_col + 1 < cols
        else:  # R
            source = blank - 1
            legal = blank_col > 0
        if not legal or not (0 <= source < len(board)) or board[source] != tile:
            raise InputError(
                f"ход {move_number} ({tile} {direction}) недопустим: "
                "указанная фишка не соседствует с пустой клеткой в этом направлении"
            )
        board[blank], board[source] = board[source], board[blank]
        blank = source
        total_cost += costs_by_direction[direction]

    placed = sum(1 for index, tile in enumerate(board) if tile != 0 and tile == index + 1)
    score = 100 * placed - total_cost
    solved = placed == puzzle.tile_count
    return CheckResult(tuple(board), len(puzzle.order), placed, total_cost, score, solved)

def _format_board(board: Sequence[int], rows: int, cols: int) -> str:
    return "\n".join(
        " ".join("=" if value == 0 else str(value)
                 for value in board[r * cols:(r + 1) * cols])
        for r in range(rows)
    )

def check_solved(path: str | Path, verbose=False) -> Tuple[bool, int]:
    puzzle = _read_puzzle(path)
    checked = _check_order(puzzle)

    if verbose:
        verdict = "SOLVED (пятнашки сошлись)" if checked.solved else "NOT SOLVED (пятнашки не сошлись)"
        print(
            f"CHECK: VALID; {verdict}; moves={checked.valid_moves}; "
            f"placed={checked.placed_tiles}/{puzzle.tile_count}; "
            f"cost={checked.total_cost}; F={checked.score}"
        )
        print("Итоговое поле:")
        print(_format_board(checked.final_board, puzzle.rows, puzzle.cols))

    return checked.solved, checked.score
