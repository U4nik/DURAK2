# Реализация сцен (StartScreen)

## Изменения в файлах

### ux.h
- `UxMode` — добавлены `StartScreen`, `Settings`, `Rules`, `Authors` (удалён `ConfirmExit`)
- `ScResult` — enum результата: `None`, `Resume`, `NewGame`, `Back`, `Exit`
- Добавлены функции:
  - `ux_wait_start_screen(canResume)` — блокирующий показ StartScreen
  - `ux_is_menu_active()` — true если в любом меню
  - `ux_reset_visuals()` — сброс рук/стола/слотов/лейаута для новой игры

### ux.cpp
- **UxMode**: `ConfirmExit` → `StartScreen`/`Settings`/`Rules`/`Authors`
- **Обработчик ESC**:
  - Idle/WaitPlayerMove → StartScreen (сохраняет `g_scPrevUxMode`)
  - GameOver → игнорируется (игрок жмёт Continue)
  - Settings/Rules/Authors → назад в StartScreen
  - StartScreen → `g_shouldExit` (выход)
- **Клик-хендлер StartScreen**:
  - `canResume=false` (главное меню): 4 пункта — «Начать новую партию», «Настройки», «Правила игры», «Об авторах»
  - `canResume=true` (in-game): 2 пункта — «Вернуться к игре», «Вернуться в основное меню»
- **`ux_draw_frame()`**: вместо ConfirmExit рисует StartScreen/Settings/Rules/Authors
- **`ux_wait_start_screen(canResume)`**: блокирующий цикл (аналог `ux_wait_gameover_continue`)
- **`ux_wait_gameover_continue()`**: досрочный выход при активации меню
- **`ux_reset_visuals()`**: `reset_all_visuals()` + перезагрузка layout
- **`reset_all_visuals()`**: убран сброс ConfirmExit-переменных

### main.cpp
- **Внешний цикл**: `ux_wait_start_screen(false)` → `ux_reset_visuals()` → `init_first_game_state()` → игра
- **Внутренний цикл**: существующий game loop
- **ESC во время игры**: `get_player_move_index()` возвращает -1 при `ux_is_menu_active()`, game loop вызывает `ux_wait_start_screen(true)`
  - `Resume` → continue (игра продолжается)
  - `Back` → break во внешний цикл (главное меню)
  - `Exit` → break
- **GameOver**: ESC игнорируется, Continue → следующая раздача

## Навигация сцен

```
┌─────────────────────────────────────────────────────┐
│  StartScreen (canResume=false)                      │
│  ┌───────────────────────────────────────────────┐  │
│  │  ПОДКИДНОЙ ДУРАК                              │  │
│  │                                               │  │
│  │  Начать новую партию   ───→ game init ───→ Idle │  │
│  │  Настройки             ───→ Settings ── ESC/кл ─┐│  │
│  │  Правила игры          ───→ Rules    ── ESC/кл ─┤│  │
│  │  Об авторах            ───→ Authors  ── ESC/кл ─┤│  │
│  │                                               │  │
│  │  ESC → g_shouldExit (выход)                   │  │
│  └───────────────────────────────────────────────┘  │
└───────────────────────┬─────────────────────────────┘
                        │ "Начать новую партию"
                        ↓
              ┌─────────────────────┐
              │  GAME (Idle/        │
              │  WaitPlayerMove)    │
              │                     │
              │  ESC → StartScreen  │
              │  (canResume=true)   │
              │                     │
              │  GameOver → Continue│──→ следующая раздача
              │           ESC игнор │
              └─────────┬───────────┘
                        │ ESC
                        ↓
┌─────────────────────────────────────────────────────┐
│  StartScreen (canResume=true)                       │
│  ┌───────────────────────────────────────────────┐  │
│  │  ПОДКИДНОЙ ДУРАК                              │  │
│  │                                               │  │
│  │  Вернуться к иге           ─── Resume ───→ Idle│  │
│  │  Вернуться в основное меню ─── Back ───→ верх │  │
│  │                                               │  │
│  │  ESC → g_shouldExit (выход)                   │  │
│  └───────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────┘
```

## Детали

### StartScreen (canResume=false) — главное меню
- Показывается при первом запуске и при выборе «Вернуться в основное меню» из in-game StartScreen
- 4 пункта: Начать новую партию, Настройки, Правила игры, Об авторах
- «Начать новую партию»:
  1. `ux_reset_visuals()` — очистка рук/стола/слотов/лейаута
  2. `init_first_game_state()` — новый State
  3. `print_ux_diff()` — раздача карт
  4. Вход во внутренний игровой цикл
- Настройки/Правила/Авторы — заглушки, клик/ESC → назад в StartScreen
- ESC → выход из программы

### StartScreen (canResume=true) — in-game меню
- Показывается при ESC во время игры (Idle/WaitPlayerMove)
- 2 пункта: Вернуться к игре, Вернуться в основное меню
- «Вернуться к игре»: `g_uxMode = g_scPrevUxMode` (восстановление Idle/WaitPlayerMove)
- «Вернуться в основное меню»: `ScResult::Back` → break во внешний цикл, StartScreen(canResume=false)

### GameOver
- ESC игнорируется (не перехватывается)
- Continue → `ux_wait_gameover_continue()` → полный сброс + `load_layout()` + переход к следующей раздаче
- Из следующей раздачи ESC работает как обычно → in-game StartScreen

### Resume (безопасность)
- `State st` живёт только в main.cpp на стеке
- StartScreen его не трогает и не знает о нём
- Resume = `g_uxMode = g_scPrevUxMode` (восстанавливает Idle/WaitPlayerMove/GameOver)
- Игра продолжается с того же места (руки, стол, колода — без изменений)

### Settings / Rules / Authors
- Режимы `UxMode::Settings`, `Rules`, `Authors` — пока заглушки
- Рисуют текст-заглушку, клик/ESC → возврат в StartScreen (canResume=false)
- В будущем — полноценные сцены с текстом правил/авторов и настройками

## Файлы
- `ux.h` — публичный API (ScResult, ux_wait_start_screen, ux_is_menu_active, ux_reset_visuals)
- `ux.cpp` — вся логика StartScreen, Settings, Rules, Authors
- `main.cpp` — внешний/внутренний цикл
