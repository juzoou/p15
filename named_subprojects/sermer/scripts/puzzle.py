from dataclasses import dataclass
from typing import Optional, Sequence, Tuple

DIRECTIONS = ("U", "D", "L", "R")
Board = Tuple[int, ...]
Move = Tuple[int, str]

@dataclass(frozen=True)
class PuzzleInput:
    rows: int
    cols: int
    board: Board
    costs: Tuple[int, int, int, int]  # U, D, L, R
    order: Optional[Tuple[Move, ...]]
    had_cost_block: bool = False

    @property
    def tile_count(self) -> int:
        return self.rows * self.cols - 1