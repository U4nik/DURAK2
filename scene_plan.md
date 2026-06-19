# Scene Architecture Plan

## UxMode
```cpp
enum class UxMode {
    StartScreen,    // стартовое меню
    Settings,       // настройки
    Rules,          // правила игры
    Authors,        // об авторах
    Idle,           // игра идёт
    WaitPlayerMove, // выбор карты
    GameOver        // game over (без изменений)
};
```
`ConfirmExit` удалён.

## Navigation

```
StartScreen (canResume=false)
  ├── "Начать новую партию" → Idle → игра
  │         ├── GameOver → continue → Idle (следующая раздача, как сейчас)
  │         ├── ESC → StartScreen(canResume=true) → "Вернуться в игру" → Idle
  │         └── ESC на GameOver → StartScreen(canResume=true, стоп звуки/салют)
  │                            → "Вернуться в игру" → GameOver (восст. звуки)
  ├── "Настройки" → SettingsScreen → ESC/"Возврат в меню" → StartScreen
  ├── "Правила игры" → RuleScreen → ESC/"Возврат в меню" → StartScreen
  ├── "Об авторах" → AuthorScreen → ESC/"Возврат в меню" → StartScreen
  └── ESC → fade-out → выход из программы
```

## Screens detail

### StartScreen — layers
1. Background (`cards/background.png`)
2. 6 cards fan (3 pairs left side, random, rotated)
3. Title "ПОДКИДНОЙ ДУРАК" (center)
4. Menu items (5): Вернуться в игру, Начать новую партию, Настройки, Правила игры, Об авторах
5. Logo `emotion/logo_start_game3.png` (bottom-right, scaled)
6. Stats (g_statsText)

### SettingsScreen — layers
1. Background
2. Title "НАСТРОЙКИ"
3. Subtitle "Сортировка карт в руке"
4. Menu: Возврат в меню, Козыри (left/right/none), Номинал (asc/desc), Сортировка по (rank/suit), Звук (on/off), Подсказка (on/off), Сброс статистики
5. Logo (top-right, scaled)
6. Background

### RuleScreen
1. Background
2. Rules text (from `rules_text.h`)
3. "Возврат в меню"

### AuthorScreen
1. Background
2. Authors text (from `authors_text.h`)
3. "Возврат в меню"

### GameOver — unchanged
- Stops sounds/fireworks on ESC → StartScreen
- Resumes sounds on "Вернуться в игру"

## Data files

**`layout_scenes.json`** — positions of all UI elements for all scenes.

**`rules_text.h`** — rules text constant.
**`authors_text.h`** — authors text constant.

## Main loop flow (main.cpp)

```
outer_loop:
    ux_wait_start_screen(false);      // первый старт
    if (exit) break;
    
    inner_loop (game):
        init/continue game
        game loop (as now)
        if (ESC) {
            g_prevUxMode = Idle;       // внутри игры
            ux_wait_start_screen(true);
            if (choseResume) continue game;
            if (choseNewGame) break to outer loop;
            if (exit) break all;
        }
        if (GameOver) {
            g_gameOverActive = true;
            ux_wait_gameover_continue();
            continue inner_loop;       // следующая раздача
        }
    
    // после inner_loop — опять StartScreen
```
