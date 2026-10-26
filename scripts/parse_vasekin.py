from dataclasses import dataclass
from pathlib import Path
from typing import List, Optional, Tuple
import re
from collections import Counter
from puzzle import PuzzleInput, Move, DIRECTIONS

class InputError(ValueError):
    """Некорректный входной файл или недопустимый ORDER."""

def _tokens_with_lines(text: str) -> List[Tuple[str, int]]:
    """Удаляет комментарии и делит текст на токены, сохраняя номера строк.

    Слэш всегда отдельный токен даже в записи вида ``1/``.
    """
    result: List[Tuple[str, int]] = []
    for line_no, line in enumerate(text.splitlines(), 1):
        line = line.split("--", 1)[0]
        for token in re.findall(r"/|[^\s/]+", line):
            result.append((token, line_no))
    return result

def _integer(token: str, line: int, description: str) -> int:
    try:
        return int(token)
    except ValueError as exc:
        raise InputError(f"строка {line}: ожидалось целое число ({description}), получено {token!r}") from exc

def _parse_input(text: str) -> PuzzleInput:
    tokens = _tokens_with_lines(text)
    if not tokens:
        raise InputError("входной файл пуст")

    pos = 0
    data_values: Optional[List[Tuple[str, int]]] = None
    costs: Optional[Tuple[int, int, int, int]] = None
    had_cost = False
    order_values: Optional[List[Tuple[str, int]]] = None
    seen_sections = set()

    while pos < len(tokens):
        section, section_line = tokens[pos]
        pos += 1
        key = section.upper()
        if key not in {"DATA", "COST", "ORDER"}:
            raise InputError(f"строка {section_line}: неизвестный блок или лишний токен {section!r}")
        if key in seen_sections:
            raise InputError(f"строка {section_line}: блок {key} указан более одного раза")
        seen_sections.add(key)

        body: List[Tuple[str, int]] = []
        while pos < len(tokens) and tokens[pos][0] != "/":
            body.append(tokens[pos])
            pos += 1
        if pos >= len(tokens) or tokens[pos][0] != "/":
            raise InputError(f"блок {key} (строка {section_line}) не завершён символом '/'")
        pos += 1

        if key == "DATA":
            data_values = body
        elif key == "COST":
            had_cost = True
            if len(body) != 4:
                raise InputError(f"блок COST должен содержать ровно 4 числа в порядке U D L R; получено {len(body)}")
            parsed = tuple(_integer(tok, ln, "стоимость") for tok, ln in body)
            if any(value < 0 for value in parsed):
                raise InputError("стоимости ходов должны быть неотрицательными")
            costs = parsed  # type: ignore[assignment]
        else:
            order_values = body

    if data_values is None:
        raise InputError("отсутствует обязательный блок DATA")

    if len(data_values) < 3 or data_values[0][0].upper() != "SIZE":
        raise InputError("первой записью блока DATA должна быть 'SIZE N M'")
    _, size_line = data_values[0]
    if len(data_values) < 3:
        raise InputError(f"строка {size_line}: после SIZE нужны два размера")
    rows = _integer(data_values[1][0], data_values[1][1], "число строк N")
    cols = _integer(data_values[2][0], data_values[2][1], "число столбцов M")
    if not (1 <= rows <= 100 and 1 <= cols <= 100):
        raise InputError("N и M должны находиться в диапазоне от 1 до 100")
    if rows == cols:
        raise InputError("по условиям задания количество строк и столбцов должно различаться (N != M)")
    if rows * cols > 10000:
        raise InputError("поле не должно содержать более 10 000 клеток")
    if rows * cols < 2:
        raise InputError("поле должно содержать хотя бы одну фишку и одну свободную клетку")

    cell_tokens = data_values[3:]
    if len(cell_tokens) != rows * cols:
        raise InputError(
            f"блок DATA: для поля {rows}x{cols} нужно {rows * cols} значений, получено {len(cell_tokens)}"
        )
    board_list: List[int] = []
    for tok, line_no in cell_tokens:
        if tok == "=":
            board_list.append(0)
        else:
            board_list.append(_integer(tok, line_no, "номер фишки или '=' для пустой клетки"))

    expected = set(range(rows * cols))
    if len(board_list) != len(expected) or set(board_list) != expected:
        missing = sorted(expected - set(board_list))
        duplicates = sorted(value for value, count in Counter(board_list).items() if count > 1)
        details = []
        if missing:
            details.append(f"отсутствуют {missing[:10]}")
        if duplicates:
            details.append(f"повторяются {duplicates[:10]}")
        raise InputError("поле должно содержать числа 1..N*M-1 и ровно один 0; " + "; ".join(details))

    parsed_order: Optional[Tuple[Move, ...]] = None
    if order_values is not None:
        if len(order_values) % 2:
            last_token, line_no = order_values[-1]
            raise InputError(f"строка {line_no}: в ORDER ожидалась пара 'номер_фишки направление', лишний токен {last_token!r}")
        moves: List[Move] = []
        for i in range(0, len(order_values), 2):
            tile_token, tile_line = order_values[i]
            direction_token, direction_line = order_values[i + 1]
            tile = _integer(tile_token, tile_line, "номер фишки в ORDER")
            direction = direction_token.upper()
            if direction not in DIRECTIONS:
                raise InputError(f"строка {direction_line}: направление должно быть U, D, L или R")
            moves.append((tile, direction))
        parsed_order = tuple(moves)

    return PuzzleInput(
        rows=rows,
        cols=cols,
        board=tuple(board_list),
        costs=costs if costs is not None else (1, 1, 1, 1),
        order=parsed_order,
        had_cost_block=had_cost,
    )

def _parsed_to_simplified(puzzle, simplified_path=None):
    if simplified_path is None:
        simplified_path = f"test_{puzzle.cols}x{puzzle.rows}.txt"

    text = f'{" ".join(map(str, puzzle.costs))} {puzzle.cols} {puzzle.rows} {" ".join(map(str, puzzle.board))}'

    try:
        output_path = Path(simplified_path)

        # Создаём родительские директории, если они отсутствуют.
        output_path.parent.mkdir(parents=True, exist_ok=True)

        # Записываем данные в файл.
        output_path.write_text(text, encoding="utf-8")

    except OSError as exc:
        raise OSError(
            f"Не удалось записать файл {simplified_path}: {exc}"
        ) from exc


def vasekin_input_to_simplified(path_to_input: str, path_to_output: str = None):
    try:
        text = Path(path_to_input).read_text(encoding="utf-8")
    except FileNotFoundError as exc:
        raise FileNotFoundError(
            f"Входной файл не найден: {path_to_input}"
        ) from exc
    except PermissionError as exc:
        raise PermissionError(
            f"Нет прав на чтение файла: {path_to_input}"
        ) from exc
    except UnicodeDecodeError as exc:
        raise ValueError(
            f"Файл {path_to_input} не является корректным UTF-8 файлом"
        ) from exc
    except OSError as exc:
        raise OSError(
            f"Ошибка чтения файла {path_to_input}: {exc}"
        ) from exc

    puzzle = _parse_input(text)

    _parsed_to_simplified(puzzle, path_to_output)