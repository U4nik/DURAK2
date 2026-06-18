# Сортировка карт в руке игрока

## Настройки (game.ini)

```ini
card_sort_mode=rank             # rank | suit
card_sort_direction=asc         # asc | desc
card_sort_trump_position=none   # none | left | right
```

## Правила

### 1. `card_sort_mode` — первичный ключ сортировки

| mode | Приоритет |
|---|---|
| `rank` | По номиналу (6..A). При равном номинале — по масти (c<d<h<s) |
| `suit` | По масти (c<d<h<s). Внутри одной масти — по номиналу |

### 2. `card_sort_direction` — направление для ВСЕХ сортировок

| direction | Ранги | Масти |
|---|---|---|
| `asc` | 6 → A | c → d → h → s |
| `desc` | A → 6 | s → h → d → c |

### 3. `card_sort_trump_position` — положение козырей (если `none` — игнорируется)

| position | Что делает |
|---|---|
| `none` | Козыри не выделяются, вся рука сортируется единым порядком |
| `left` | Все козыри слева, затем не-козыри |
| `right` | Все не-козыри, затем козыри справа |

## Таблица комбинаций (trump=♥)

| mode | dir | trump pos | Результат |
|---|---|---|---|
| `rank` | `asc` | `none` | `7♣ 7♠ 8♥ 9♠ J♠ A♠` |
| `rank` | `asc` | `left` | `7♠ 9♠ J♠ A♠ 7♣ 8♥` |
| `rank` | `asc` | `right` | `7♣ 8♥ 7♠ 9♠ J♠ A♠` |
| `rank` | `desc` | `none` | `A♠ J♠ 9♠ 8♥ 7♠ 7♣` |
| `rank` | `desc` | `left` | `A♠ J♠ 9♠ 7♠ 8♥ 7♣` |
| `rank` | `desc` | `right` | `8♥ 7♣ A♠ J♠ 9♠ 7♠` |
| `suit` | `asc` | `none` | `7♣ 8♥ 7♠ 9♠ J♠ A♠` (♣→♥→♠) |
| `suit` | `asc` | `left` | `7♥ 8♥ 7♣ 7♠ 9♠ J♠` (♥→♣→♠) |
| `suit` | `asc` | `right` | `7♣ 7♠ 9♠ J♠ 7♥ 8♥` (♣→♠→♥) |
| `suit` | `desc` | `none` | `A♠ J♠ 9♠ 7♠ 8♥ 7♣` (♠→♥→♣) |
| `suit` | `desc` | `left` | `8♥ 7♥ A♠ J♠ 9♠ 7♠ 7♣` (♥→♠→♣) |
| `suit` | `desc` | `right` | `A♠ J♠ 9♠ 7♠ 7♣ 8♥ 7♥` (♠→♣→♥) |

## Алгоритм (компаратор)

```
bool comp(a, b):
    aTrump = a.suit == trumpSuit
    bTrump = b.suit == trumpSuit

    // 1. Trump position (если не none)
    if aTrump != bTrump && trumpPos != none:
        if trumpPos == left:  return aTrump   // козыри слева
        if trumpPos == right: return bTrump   // козыри справа

    // 2. Сортировка внутри группы
    if mode == rank:
        if rank !=: return asc ? a.rank < b.rank : a.rank > b.rank
        return asc ? a.suit < b.suit : a.suit > b.suit

    if mode == suit:
        if suit !=: return asc ? a.suit < b.suit : a.suit > b.suit
        return asc ? a.rank < b.rank : a.rank > b.rank
```
