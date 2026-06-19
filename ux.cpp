#include <SFML/Graphics.hpp>
#include <SFML/Audio.hpp>
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cmath>
#include <random>
#include <queue>
#include <windows.h>
#include "ux.h"
#include "salut.h"
using namespace std;

// ------------------------------------------------------------
// CARD STRUCTURE (из движка)
// ------------------------------------------------------------
struct Card
{
    int rank; // 6..14
    int suit; // 0..3
};

// -------------------------
// SIDE (из движка)
// -------------------------
enum Side
{
    PLR,
    BOT
};

// ------------------------------------------------------------
// LAYOUT STRUCT
// ------------------------------------------------------------
struct Layout
{
    float plr_y = 900;
    float bot_y = 120;
    float center_x = 960;
    float center_y = 540; // вертикальный центр стола

    sf::Vector2f deck_pos;
    float deck_angle = 0;

    sf::Vector2f trump_pos;
    float trump_angle = 0;

    sf::Vector2f action_pos;
    bool has_action = false;

    sf::Vector2f discard_exit;
};

enum CardState
{
    InHand,
    MovingToSlot,
    InSlot,
    MovingToDiscard
};

struct CardVisual
{
    sf::Sprite sprite;
    Card card;
    sf::Vector2f currentPos;
    sf::Vector2f targetPos;
    bool animating = false;
    CardState state = InHand;
    Side owner = PLR;           // PLR или BOT
    int tablePairIndex = -1;    // индекс PairSlot в State.table (если в слоте)
    bool flipMidFlight = false; // для бота: перевернуть в полёте
    float initialDist = 0.f;    // для mid-flight threshold
    float alpha = 255.f;        // для fade-out
    float rotSpeed = 0.f;       // скорость вращения при отбое
    float liftOffset = 0.f;
    float shakeOffset = 0.f;
    bool lifting = false;
    bool shaking = false;
    float liftTimer = 0.f;
    float shakeTimer = 0.f;
    bool hovered = false;
    float delay = 0.f; // задержка перед стартом анимации  раздачи
};

struct Slot
{
    sf::Vector2f pos;
    bool occupied = false;
    int pairIndex = -1; // индекс PairSlot в State.table
    bool isAttack = true;
    bool faceUp = true;
};

// ------------------------------------------------------------
// UX COMMAND
// ------------------------------------------------------------

static std::queue<UxCommand> g_cmdQueue;
static float g_waitTimer = 0.f;

// ------------------------------------------------------------
// UX MODE / PENDING MOVES
// ------------------------------------------------------------
struct PendingMoves
{
    std::vector<std::string> moves; // строки ходов
    bool active = false;
};

enum class UxMode
{
    Idle,
    WaitPlayerMove,
    GameOver,
    StartScreen,
    Settings,
    Rules,
    Authors

};

// ------------------------------------------------------------
// ГЛОБАЛЬНОЕ СОСТОЯНИЕ UX-МОДУЛЯ
// ------------------------------------------------------------

// layout
static Layout g_layout;

// визуальное состояние
static vector<CardVisual> g_vis_plr;
static vector<CardVisual> g_vis_bot;
static vector<CardVisual> g_tableVisuals;

static vector<Slot> g_attackSlots;
static vector<Slot> g_defendSlots;

// anchors для рук, заполняются из JSON (используем только Y)
static vector<float> g_handAnchorsPlrY;
static vector<float> g_handAnchorsBotY;

// флаг принудительного завершения (ESC)
bool g_shouldExit = false;

// флаги отложенной сортировки (после завершения всех анимаций)
static bool g_pendingSortPlr = false;
static bool g_pendingSortBot = false;

// ------------------------------------------------------------
// Проверка: есть ли активные анимации
// ------------------------------------------------------------
static bool any_card_animating(const std::vector<CardVisual> &v)
{
    for (auto &c : v)
        if (c.animating) // ← твой флаг
            return true;
    return false;
}

static bool animations_active()
{
    return any_card_animating(g_vis_plr) || any_card_animating(g_vis_bot) || any_card_animating(g_tableVisuals);
}

// текстуры
sf::Font g_font;
sf::Texture g_texBg;
sf::Sprite g_sprBg;

sf::Texture g_texBack;

sf::Texture g_cardTex[4][9]; // 6..14 → 9 карт

sf::Texture g_texTake;
sf::Texture g_texBeat;
sf::Texture g_texGive;
sf::Texture g_texContinue;

sf::Sprite g_sprTake;
sf::Sprite g_sprBeat;
sf::Sprite g_sprGive;

sf::Sprite g_trumpSpr;
sf::Sprite g_deckSpr;
sf::Sprite g_sprAction;       // кнопка для игры (Бито/Взять/Продолжить Game Over)
sf::Sprite g_sprExitContinue; // кнопка для сцены выхода (Продолжить)
sf::Sprite g_sprTrumpSuit;    // спрайт масти козыря (когда колода пуста)

// текстуры мастей козырей
sf::Texture g_texCSuite; // трефы
sf::Texture g_texDSuite; // бубны
sf::Texture g_texHSuite; // червы
sf::Texture g_texSSuite; // пики

// флаги видимости
static bool g_trumpVisible = true;
static bool g_deckVisible = true;
static int g_deckSize = 24;

static sf::SoundBuffer g_soundBufferBito;
static sf::Sound g_soundBito;

static sf::SoundBuffer g_soundBufferFromKol;
static sf::Sound g_soundFromKol;

static sf::SoundBuffer g_soundBufferFallDown;
static sf::Sound g_soundFallDown;

static sf::SoundBuffer g_soundBufferTake;
static sf::Sound g_soundTake;

static sf::SoundBuffer g_soundBufferKoloda;
static sf::Sound g_soundKoloda;

static sf::SoundBuffer g_soundBufferRocket;
static sf::Sound g_soundRocket;

static sf::SoundBuffer g_soundBufferExplosion;
static sf::Sound g_soundExplosion;

// текст количества карт в колоде
static sf::Text g_deckCountText;

// текст статистики игр
static sf::Text g_statsText;

// эмоция — картинка (левый верхний угол)
static sf::Texture g_texEmotion[3];
static sf::Sprite g_sprEmotion;

static int g_statsPlrWins = 0;
static int g_statsBotWins = 0;
static int g_statsTotal = 0;

// текст подсказки игроку
static sf::Text g_hintText;
static bool g_hintVisible = false; // видима ли подсказка
static bool g_hintFading = false;  // флаг затухания
static float g_hintAlpha = 255.f;  // альфа для затухания

bool g_gameOverActive = false;
bool g_gameOverContinueClicked = false;

sf::Text g_gameOverText;

// сцена подтверждения выхода (ESC)
static sf::Text g_exitConfirmTextQuestion;    // "Покинуть игру?"
static sf::Text g_exitConfirmTextInstruction; // "Выход - нажмите ESC..."
static bool g_exitConfirmActive = false;
static float g_exitConfirmAlpha = 255.f;
static bool g_exitConfirmFading = false;
static bool g_exitConfirmed = false;           // true если игрок нажал ESC для выхода
static float g_exitConfirmDelayTimer = 0.f;    // задержка 1 сек перед "Пока..."
static float g_exitBgAlpha = 255.f;            // альфа для фона и всех элементов сцены
static UxMode g_exitPrevUxMode = UxMode::Idle; // ← сохраняем режим до выхода
static float g_exitButtonAlpha = 255.f;        // альфа для кнопки выхода
static bool g_exitButtonFading = false;        // флаг затухания кнопки выхода
static bool g_exitButtonVisible = false;       // видима ли кнопка выхода

// флаг показа масти козыря
static bool g_showTrumpSuit = true; // ← легко отключить эффект

// ------------------------------------------------------------
// START SCREEN GLOBALS
// ------------------------------------------------------------
static ScResult g_startScreenResult = ScResult::None;
static bool g_scCanResume = false;
static UxMode g_scPrevUxMode = UxMode::Idle; // режим до StartScreen
static sf::Text g_scTitleText;
static sf::Text g_scMenuText;
static sf::Texture g_scLogoTexture;
static sf::Sprite g_scLogoSprite;
static bool g_scLogoLoaded = false;

// 6 карт-пар на StartScreen (3 пары, левая сторона)
static std::vector<sf::Sprite> g_scCards;
static bool g_scCardsInit = false;

// StartScreen layout (заполняется load_start_screen_layout)
static struct {
    sf::Vector2f titlePos;
    int titleSize;
    sf::Vector2f logoPos;
    float logoScale;
    std::string logoFile;
    std::vector<sf::Vector2f> menuPos;
    std::vector<sf::Vector2f> cardsPos;
} g_scLayout;

// меню на StartScreen (canResume=false — главное меню)
static const int SC_MAIN_COUNT = 4;
static const wchar_t* SC_MAIN_ITEMS[SC_MAIN_COUNT] = {
    L"Начать новую партию",
    L"Настройки",
    L"Правила игры",
    L"Об авторах"
};

// меню на in-game StartScreen (canResume=true)
static const int SC_RESUME_COUNT = 2;
static const wchar_t* SC_RESUME_ITEMS[SC_RESUME_COUNT] = {
    L"Вернуться к игре",
    L"Вернуться в основное меню"
};

// состояние action-кнопки
static std::string g_actionButtonState = "NONE";
static float g_actionButtonAlpha = 255.f;     // для fade-анимации
static bool g_actionButtonFading = false;     // флаг затухания
static bool g_actionButtonWasVisible = false; // была ли кнопка видима игроку

// ------------------------------------------------------------
// НАСТРОЙКИ ИГРЫ (game.ini)
// ------------------------------------------------------------
static struct {
    bool card_hint_enabled = true;
    bool sound_effects = true;
    std::string card_sort_mode = "rank";
    std::string card_sort_direction = "asc";
    std::string card_sort_trump_position = "none";
} g_settings;

static void load_settings()
{
    std::ifstream f("game.ini");
    if (!f.is_open())
        return;

    std::string line;
    while (std::getline(f, line))
    {
        size_t eq = line.find('=');
        if (eq == std::string::npos)
            continue;

        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);

        if (key == "card_hint_enabled")
            g_settings.card_hint_enabled = (val != "0");
        else if (key == "sound_effects")
            g_settings.sound_effects = (val != "0");
        else if (key == "card_sort_mode")
            g_settings.card_sort_mode = val;
        else if (key == "card_sort_direction")
            g_settings.card_sort_direction = val;
        else if (key == "card_sort_trump_position")
            g_settings.card_sort_trump_position = val;
    }
    f.close();
}

// звуки — обёртка с проверкой настройки
static void play_sound(sf::Sound &s)
{
    if (g_settings.sound_effects)
        s.play();
}

// UX режим ожидания хода
static PendingMoves g_pending;
static UxMode g_uxMode = UxMode::Idle;
static int g_chosenMove = -1;
static bool g_moveReady = false;

// масть козыря (для сортировки)
static int g_trumpSuit = 0;

// таймер кадра
static sf::Clock g_clock;

// салют (Game Over)
static FireworksManager *g_fireworks = nullptr;
static bool g_gameOverWithFireworks = false; // салют активен в Game Over
static bool g_rocketSoundPlayed = false; // звук ракеты сыгран 1 раз за сессию

// ------------------------------------------------------------
// FORWARD DECLARATIONS
// ------------------------------------------------------------
static Layout load_layout();
static void load_start_screen_layout();

static void layout_hand(
    vector<CardVisual> &hand,
    float centerX,
    float y,
    const vector<float> &anchorsY);

static void animate_cards(
    vector<CardVisual> &hand,
    sf::Texture cardTex[4][9],
    const sf::Texture &texBack,
    const Layout &L);

static void update_hover_effects(
    std::vector<CardVisual> &hand,
    const std::vector<std::string> &validMoves,
    sf::Vector2f mousePos);

static void update_card_effects(
    std::vector<CardVisual> &hand,
    float dt);

static void add_card_to_hand(
    vector<CardVisual> &hand,
    const Card &c,
    sf::Texture cardTex[4][9],
    const sf::Texture &texBack,
    const sf::Vector2f &deckPos,
    float centerX,
    float y,
    bool faceUp,
    Side owner);

static void play_card_to_slot(
    vector<CardVisual> &hand,
    int handIndex,
    vector<Slot> &slots,
    int slotIndex,
    int pairIndex,
    bool faceUpForBot,
    sf::Texture cardTex[4][9],
    const sf::Texture &texBack);

static void start_discard_animation(const Layout &L);

static void start_table_to_hand(
    Side taker,
    vector<CardVisual> &vis_plr,
    vector<CardVisual> &vis_bot,
    sf::Texture cardTex[4][9],
    const sf::Texture &texBack,
    const Layout &L);

static Card parse_card(const std::string &s);

static bool anyAnimating(
    const vector<CardVisual> &a,
    const vector<CardVisual> &b,
    const vector<CardVisual> &t);

static std::string ux_card_to_string(const Card &c);

static int ux_find_move_index_for_string(const std::string &s);
static int ux_find_pass_index();

// run_command — визуальное применение команды
void ux_run_command(const std::string &name,
                    const std::vector<std::string> &args)
{
    g_cmdQueue.push(UxCommand{name, args});
}

// отрисовка всего кадра
static void ux_draw_frame();

// обработка событий SFML
static void ux_handle_events();

// ------------------------------------------------------------
// СТАТИСТИКА ИГРЫ (загрузка/сохранение)
// ------------------------------------------------------------
static void load_stats()
{
    std::ifstream f("stat.ini");
    if (!f.is_open())
    {
        // файл не найден — используем значения по умолчанию
        g_statsPlrWins = 0;
        g_statsBotWins = 0;
        g_statsTotal = 0;
        return;
    }

    std::string line;
    while (std::getline(f, line))
    {
        size_t eq = line.find('=');
        if (eq == std::string::npos)
            continue;

        std::string key = line.substr(0, eq);
        int val = std::stoi(line.substr(eq + 1));

        if (key == "plr_wins")
            g_statsPlrWins = val;
        else if (key == "bot_wins")
            g_statsBotWins = val;
        else if (key == "total")
            g_statsTotal = val;
    }
    f.close();
}

static void save_stats()
{
    std::ofstream f("stat.ini");
    f << "plr_wins=" << g_statsPlrWins << "\n";
    f << "bot_wins=" << g_statsBotWins << "\n";
    f << "total=" << g_statsTotal << "\n";
    f.close();
}

static void update_stats_text()
{
    std::wstring leader;
    if (g_statsPlrWins > g_statsBotWins)
        leader = L"выигрывает игрок";
    else if (g_statsBotWins > g_statsPlrWins)
        leader = L"выигрывает бот";
    else
        leader = L"ничья";

    std::wstring stats = L"СЧЁТ " + std::to_wstring(g_statsPlrWins) + L" : " +
                         std::to_wstring(g_statsBotWins) + L" из " +
                         std::to_wstring(g_statsTotal) + L" [" + leader + L"]";
    g_statsText.setString(stats);

    // выравнивание по правому краю
    auto b = g_statsText.getLocalBounds();
    g_statsText.setOrigin(b.width, 0.f);
}

static void record_game_result(bool plrWon)
{
    g_statsTotal++;
    if (plrWon)
        g_statsPlrWins++;
    else
        g_statsBotWins++;

    save_stats();
    update_stats_text();
}

static void record_draw()
{
    g_statsTotal++;
    save_stats();
    update_stats_text();
}

// ------------------------------------------------------------
// ПОДСКАЗКА ИГРОКУ (в начале хода)
// ------------------------------------------------------------
static void show_hint_text()
{
    g_hintText.setString(L"Твой ход, выбери карту и ходи");

    // центрируем
    auto b = g_hintText.getLocalBounds();
    g_hintText.setOrigin(b.width / 2.f, b.height / 2.f);
    g_hintText.setPosition(1920.f / 2.f, 1080.f / 2.f);

    g_hintAlpha = 255.f;
    g_hintVisible = true;
    g_hintFading = false;
}

static void hide_hint_text()
{
    if (g_hintVisible && g_hintAlpha > 0.f)
        g_hintFading = true;
}

void reset_all_visuals()
{
    g_shouldExit = false;

    g_vis_plr.clear();
    g_vis_bot.clear();
    g_tableVisuals.clear();

    g_attackSlots.clear();
    g_defendSlots.clear();

    g_pending.moves.clear();
    g_pending.active = false;

    g_actionButtonState = "NONE";
    g_actionButtonAlpha = 255.f;
    g_actionButtonFading = false;
    g_actionButtonWasVisible = false; // сброс для новой игры
    g_sprAction.setColor(sf::Color(255, 255, 255, 0));

    g_trumpVisible = true;
    g_deckVisible = true;
    g_deckSize = 24;
    g_deckCountText.setString(L"в колоде:24");

    g_deckSpr.setColor(sf::Color(255, 255, 255, 255));
    g_trumpSpr.setColor(sf::Color(255, 255, 255, 255));

    g_sprTrumpSuit.setColor(sf::Color(255, 255, 255, 0)); // скрыть спрайт масти
    g_showTrumpSuit = true;                               // разрешить показ в новой игре

    g_waitTimer = 0.f;
    g_uxMode = UxMode::Idle;
    g_gameOverActive = false;
    g_gameOverWithFireworks = false;

    // сброс стартового меню
    g_startScreenResult = ScResult::None;
    g_scCanResume = false;

    g_pendingSortPlr = false;
    g_pendingSortBot = false;

    // сброс подсказки
    g_hintVisible = false;
    g_hintFading = false;
    g_hintAlpha = 255.f;
    g_hintText.setString(L"");
}

// ------------------------------------------------------------
// ПУБЛИЧНЫЙ ИНТЕРФЕЙС UX-МОДУЛЯ
// ------------------------------------------------------------
static sf::RenderWindow *g_window = nullptr;

void ux_init(sf::RenderWindow *win)
{
    // Скрываем консольное окно
    ShowWindow(GetConsoleWindow(), SW_HIDE);

    g_window = win;

    // ---------------------------
    // LOAD BACKGROUND
    // ---------------------------
    if (!g_texBg.loadFromFile("cards/background.png"))
        std::cout << "ERROR loading cards/background.png\n";
    g_sprBg.setTexture(g_texBg);

    // ---------------------------
    // LOAD CARD BACK
    // ---------------------------
    if (!g_texBack.loadFromFile("cards/back.png"))
        std::cout << "ERROR loading cards/back.png\n";

    //----------
    // load font
    if (!g_font.loadFromFile("Symbola-AjYx.ttf"))
        std::cout << "ERROR loading Symbola-AjYx.ttf\n";

    // текст количества карт в колоде
    g_deckCountText.setFont(g_font);
    g_deckCountText.setCharacterSize(20);
    g_deckCountText.setFillColor(sf::Color(255, 165, 0)); // оранжевый
    g_deckCountText.setOutlineColor(sf::Color::Black);
    g_deckCountText.setOutlineThickness(2.f);
    g_deckCountText.setString(L"в колоде:24");
    g_deckCountText.setPosition(1802.f, 1010.f);

    // текст статистики игр
    g_statsText.setFont(g_font);
    g_statsText.setCharacterSize(20);
    g_statsText.setFillColor(sf::Color(255, 165, 0)); // оранжевый
    g_statsText.setOutlineColor(sf::Color::Black);
    g_statsText.setOutlineThickness(2.f);
    g_statsText.setPosition(1910.f, 1050.f);
    // выравнивание по правому краю будет в update_stats_text()

    // эмоция — загружаем картинки (570x570, масштаб 30%)
    g_texEmotion[0].loadFromFile("emotion/joy.png");
    g_texEmotion[1].loadFromFile("emotion/normal.png");
    g_texEmotion[2].loadFromFile("emotion/sadness.png");
    g_sprEmotion.setTexture(g_texEmotion[0]);
    g_sprEmotion.setScale(0.3f, 0.3f);
    g_sprEmotion.setPosition(20.f, 20.f);

    // загружаем статистику из файла
    load_stats();
    update_stats_text();

    g_gameOverText.setFont(g_font);
    g_gameOverText.setCharacterSize(80);
    g_gameOverText.setFillColor(sf::Color(255, 165, 0)); // оранжевый
    g_gameOverText.setOutlineColor(sf::Color::Black);
    g_gameOverText.setOutlineThickness(3.f);
    g_gameOverText.setString("");
    g_gameOverText.setPosition(1920.f / 2.f, 1080.f / 2.f - 100.f);

    // текст подтверждения выхода
    g_exitConfirmTextQuestion.setFont(g_font);
    g_exitConfirmTextQuestion.setCharacterSize(64);
    g_exitConfirmTextQuestion.setFillColor(sf::Color(255, 165, 0)); // оранжевый
    g_exitConfirmTextQuestion.setOutlineColor(sf::Color::Black);
    g_exitConfirmTextQuestion.setOutlineThickness(2.5f);
    g_exitConfirmTextQuestion.setString(L"Покинуть игру?");
    auto bq = g_exitConfirmTextQuestion.getLocalBounds();
    g_exitConfirmTextQuestion.setOrigin(bq.width / 2.f, bq.height / 2.f);
    g_exitConfirmTextQuestion.setPosition(1920.f / 2.f, 1080.f / 2.f - 150.f);

    g_exitConfirmTextInstruction.setFont(g_font);
    g_exitConfirmTextInstruction.setCharacterSize(48);
    g_exitConfirmTextInstruction.setFillColor(sf::Color(255, 165, 0)); // оранжевый
    g_exitConfirmTextInstruction.setOutlineColor(sf::Color::Black);
    g_exitConfirmTextInstruction.setOutlineThickness(2.5f);
    g_exitConfirmTextInstruction.setString(L"Выход - нажмите клавишу ESC");
    auto bi = g_exitConfirmTextInstruction.getLocalBounds();
    g_exitConfirmTextInstruction.setOrigin(bi.width / 2.f, bi.height / 2.f);
    g_exitConfirmTextInstruction.setPosition(1920.f / 2.f, 1080.f / 2.f - 80.f);

    // текст подсказки игроку
    g_hintText.setFont(g_font);
    g_hintText.setCharacterSize(64);
    g_hintText.setFillColor(sf::Color(255, 165, 0)); // оранжевый
    g_hintText.setOutlineColor(sf::Color::Black);
    g_hintText.setOutlineThickness(2.5f);
    g_hintText.setString(L"");
    g_hintText.setPosition(1920.f / 2.f, 1080.f / 2.f);

    // StartScreen — заголовок
    g_scTitleText.setFont(g_font);
    g_scTitleText.setCharacterSize(80);
    g_scTitleText.setFillColor(sf::Color(255, 165, 0));
    g_scTitleText.setOutlineColor(sf::Color::Black);
    g_scTitleText.setOutlineThickness(3.f);
    g_scTitleText.setString(L"ПОДКИДНОЙ ДУРАК");
    auto stb = g_scTitleText.getLocalBounds();
    g_scTitleText.setOrigin(stb.width / 2.f, stb.height / 2.f);
    g_scTitleText.setPosition(960.f, 120.f);

    // StartScreen — текст меню (настраивается перед отрисовкой)
    g_scMenuText.setFont(g_font);
    g_scMenuText.setCharacterSize(48);
    g_scMenuText.setFillColor(sf::Color(255, 165, 0));
    g_scMenuText.setOutlineColor(sf::Color::Black);
    g_scMenuText.setOutlineThickness(2.5f);

    // StartScreen — лого (текстура загрузится после карт)
    g_scLogoLoaded = false;
    
    // карты StartScreen — инициализируются после загрузки текстур карт

    // ---------------------------
    // LOAD CARD TEXTURES
    // ---------------------------
    auto suitChar = [&](int s)
    {
        switch (s)
        {
        case 0:
            return 'c';
        case 1:
            return 'd';
        case 2:
            return 'h';
        case 3:
            return 's';
        }
        return 'c';
    };

    auto rank_to_str = [&](int rr) -> std::string
    {
        if (rr >= 6 && rr <= 10)
            return std::to_string(rr);
        if (rr == 11)
            return "J";
        if (rr == 12)
            return "Q";
        if (rr == 13)
            return "K";
        if (rr == 14)
            return "A";
        return "?";
    };

    for (int s = 0; s < 4; s++)
    {
        for (int r = 6; r <= 14; r++)
        {
            std::string path = "cards/";
            path += rank_to_str(r);
            path += suitChar(s);
            path += ".png";

            if (!g_cardTex[s][r - 6].loadFromFile(path))
                std::cout << "ERROR loading: " << path << "\n";
        }
    }

    // ---------------------------
    // LOAD BUTTONS
    // ---------------------------
    if (!g_texTake.loadFromFile("buttons/take.png"))
        std::cout << "ERROR loading buttons/take.png\n";
    if (!g_texBeat.loadFromFile("buttons/beat.png"))
        std::cout << "ERROR loading buttons/beat.png\n";
    if (!g_texGive.loadFromFile("buttons/give.png"))
        std::cout << "ERROR loading buttons/give.png\n";
    if (!g_texContinue.loadFromFile("buttons/continue.png"))
        std::cout << "ERROR loading buttons/continue.png\n";

    g_sprTake.setTexture(g_texTake);
    g_sprBeat.setTexture(g_texBeat);
    g_sprGive.setTexture(g_texGive);

    // ---------------------------
    // LOAD TRUMP SUIT SPRITES
    // ---------------------------
    if (!g_texCSuite.loadFromFile("cards/c_suite.png"))
        std::cout << "ERROR loading cards/c_suite.png\n";
    if (!g_texDSuite.loadFromFile("cards/d_suite.png"))
        std::cout << "ERROR loading cards/d_suite.png\n";
    if (!g_texHSuite.loadFromFile("cards/h_suite.png"))
        std::cout << "ERROR loading cards/h_suite.png\n";
    if (!g_texSSuite.loadFromFile("cards/s_suite.png"))
        std::cout << "ERROR loading cards/s_suite.png\n";

    // ---------------------------
    // LOAD SOUNDS
    // ---------------------------
    if (!g_soundBufferBito.loadFromFile("sound/BITO.WAV"))
        std::cout << "ERROR loading sound/BITO.WAV\n";
    g_soundBito.setBuffer(g_soundBufferBito);

    if (!g_soundBufferFromKol.loadFromFile("sound/FROMKOL.WAV"))
        std::cout << "ERROR loading sound/FROMKOL.WAV\n";
    g_soundFromKol.setBuffer(g_soundBufferFromKol);

    if (!g_soundBufferFallDown.loadFromFile("sound/FALLDOWN.WAV"))
        std::cout << "ERROR loading sound/FALLDOWN.WAV\n";
    g_soundFallDown.setBuffer(g_soundBufferFallDown);

    if (!g_soundBufferTake.loadFromFile("sound/TAKE.WAV"))
        std::cout << "ERROR loading sound/TAKE.WAV\n";
    g_soundTake.setBuffer(g_soundBufferTake);

    if (!g_soundBufferKoloda.loadFromFile("sound/KOLODA.WAV"))
        std::cout << "ERROR loading sound/KOLODA.WAV\n";
    g_soundKoloda.setBuffer(g_soundBufferKoloda);

    if (!g_soundBufferRocket.loadFromFile("sound/rocket.wav"))
        std::cout << "ERROR loading sound/rocket.wav\n";
    g_soundRocket.setBuffer(g_soundBufferRocket);

    if (!g_soundBufferExplosion.loadFromFile("sound/explosion.wav"))
        std::cout << "ERROR loading sound/explosion.wav\n";
    g_soundExplosion.setBuffer(g_soundBufferExplosion);

    // ---------------------------
    // LOAD SETTINGS
    // ---------------------------
    load_settings();

    // ---------------------------
    // RESET ANIMATIONS
    // ---------------------------
    reset_all_visuals();
    g_layout = load_layout();

    // КОЛОДА
    g_deckSpr.setTexture(g_texBack);
    g_deckSpr.setOrigin(62, 90);
    g_deckSpr.setPosition(g_layout.deck_pos);
    g_deckSpr.setRotation(g_layout.deck_angle);

    // КОЗЫРЬ
    g_trumpSpr.setTexture(g_texBack); // или реальная карта позже
    g_trumpSpr.setOrigin(62, 90);
    g_trumpSpr.setPosition(g_layout.trump_pos);
    g_trumpSpr.setRotation(g_layout.trump_angle);

    // КНОПКА
    g_sprAction.setTexture(g_texTake); // временная текстура
    g_sprAction.setOrigin(g_texTake.getSize().x / 2, g_texTake.getSize().y / 2);
    g_sprAction.setPosition(g_layout.action_pos);

    // КНОПКА ВЫХОДА (для сцены подтверждения)
    g_sprExitContinue.setTexture(g_texContinue);
    g_sprExitContinue.setOrigin(g_texContinue.getSize().x / 2, g_texContinue.getSize().y / 2);
    g_sprExitContinue.setPosition(g_layout.action_pos);      // та же позиция
    g_sprExitContinue.setColor(sf::Color(255, 255, 255, 0)); // скрыта на старте

    // СПРАЙТ МАСТИ КОЗЫРЯ (для пустой колоды)
    g_sprTrumpSuit.setOrigin(62, 90);                     // как у карты
    g_sprTrumpSuit.setPosition(g_layout.deck_pos);        // на месте колоды
    g_sprTrumpSuit.setColor(sf::Color(255, 255, 255, 0)); // скрыт на старте

    // На старте скрываем козырь и кнопку
    g_trumpSpr.setColor(sf::Color(255, 255, 255, 0));
    g_sprAction.setColor(sf::Color(255, 255, 255, 0));

    // ---------------------------------------------------------
    // START SCREEN INIT (после загрузки карт и звуков)
    // ---------------------------------------------------------
    load_start_screen_layout();

    // Заголовок
    g_scTitleText.setString(L"ПОДКИДНОЙ ДУРАК");
    // позиция задаётся в draw из g_scLayout

    // Лого
    g_scLogoLoaded = g_scLogoTexture.loadFromFile(g_scLayout.logoFile);
    if (g_scLogoLoaded)
    {
        g_scLogoSprite.setTexture(g_scLogoTexture);
        g_scLogoSprite.setScale(g_scLayout.logoScale, g_scLayout.logoScale);
        g_scLogoSprite.setPosition(g_scLayout.logoPos);
    }

    // 6 случайных карт (3 пары слева)
    g_scCards.clear();
    {
        std::vector<int> idxs(36);
        for (int i = 0; i < 36; i++) idxs[i] = i;
        std::shuffle(idxs.begin(), idxs.end(), std::mt19937((unsigned)time(nullptr)));
        for (int i = 0; i < 6 && i < (int)g_scLayout.cardsPos.size(); i++)
        {
            int cardIdx = idxs[i];
            int suit = cardIdx / 9;
            int rank = 6 + (cardIdx % 9);
            sf::Sprite spr;
            spr.setTexture(g_cardTex[suit][rank - 6]);
            spr.setOrigin(62, 90);
            spr.setPosition(g_scLayout.cardsPos[i]);
            g_scCards.push_back(spr);
        }
    }
    g_scCardsInit = true;

    // инициализация салюта
    g_fireworks = new FireworksManager(1920.f, 1080.f);
    g_fireworks->setOnRocketLaunch([]() {
        if (!g_rocketSoundPlayed) {
            play_sound(g_soundRocket);
            g_rocketSoundPlayed = true;
        }
    });
    g_fireworks->setOnExplosion([]() { play_sound(g_soundExplosion); });
}
// ------------------------------------------------------------
// LOAD LAYOUT (layout_game.json в корне EXE)
// ------------------------------------------------------------
static Layout load_layout()
{
    Layout L;
    L.discard_exit = {-200.f, L.center_y};

    ifstream f("layout_game.json");
    if (!f.is_open())
    {
        cout << "NO JSON layout_game.json\n";
        return L;
    }

    string json((istreambuf_iterator<char>(f)), istreambuf_iterator<char>());

    auto getVal = [&](const string &key, size_t start) -> float
    {
        size_t p = json.find(key, start);
        if (p == string::npos)
            return 0;
        p = json.find(":", p);
        size_t end = json.find_first_of(",}", p + 1);
        return stof(json.substr(p + 1, end - (p + 1)));
    };

    // player hand Y
    size_t ph = json.find("\"player_hand\"");
    if (ph != string::npos)
    {
        size_t yk = json.find("\"y\"", ph);
        if (yk != string::npos)
            L.plr_y = getVal("\"y\"", ph);
    }

    // bot hand Y
    size_t bh = json.find("\"bot_hand\"");
    if (bh != string::npos)
    {
        size_t yk = json.find("\"y\"", bh);
        if (yk != string::npos)
            L.bot_y = getVal("\"y\"", bh);
    }

    // center X = середина экрана
    L.center_x = 1920.f / 2.f;

    // deck
    size_t deckPos = json.find("\"deck\"");
    if (deckPos != string::npos)
    {
        L.deck_pos.x = getVal("\"x\"", deckPos);
        L.deck_pos.y = getVal("\"y\"", deckPos);
        L.deck_angle = getVal("\"angle\"", deckPos);
    }

    // trump
    size_t trumpPos = json.find("\"trump\"");
    if (trumpPos != string::npos)
    {
        L.trump_pos.x = getVal("\"x\"", trumpPos);
        L.trump_pos.y = getVal("\"y\"", trumpPos);
        L.trump_angle = getVal("\"angle\"", trumpPos);
    }

    // action button
    size_t actPos = json.find("\"action_button\"");
    if (actPos != string::npos)
    {
        L.action_pos.x = getVal("\"x\"", actPos);
        L.action_pos.y = getVal("\"y\"", actPos);
        L.has_action = true;
    }

    // ----------------------------
    // Универсальный парсер массива { "x":..., "y":... }
    // ----------------------------
    auto parseXYArray = [&](const string &key, vector<Slot> &outSlots, bool isAttack)
    {
        size_t pos = json.find(key);
        if (pos == string::npos)
            return;

        size_t arrStart = json.find("[", pos);
        size_t arrEnd = json.find("]", arrStart);
        if (arrStart == string::npos || arrEnd == string::npos)
            return;

        string arr = json.substr(arrStart + 1, arrEnd - arrStart - 1);
        size_t p = 0;
        int idx = 0;

        while (true)
        {
            size_t xKey = arr.find("\"x\"", p);
            if (xKey == string::npos)
                break;

            size_t xColon = arr.find(":", xKey);
            size_t xEnd = arr.find_first_of(",}", xColon);
            float x = std::stof(arr.substr(xColon + 1, xEnd - (xColon + 1)));

            size_t yKey = arr.find("\"y\"", xEnd);
            if (yKey == string::npos)
                break;

            size_t yColon = arr.find(":", yKey);
            size_t yEnd = arr.find_first_of(",}", yColon);
            float y = std::stof(arr.substr(yColon + 1, yEnd - (yColon + 1)));

            Slot s;
            s.pos = {x, y};
            s.isAttack = isAttack;
            s.faceUp = true;
            s.occupied = false;
            s.pairIndex = idx++;
            outSlots.push_back(s);

            p = yEnd;
        }
    };

    // ----------------------------
    // Парсер массива рук (берём только Y)
    // ----------------------------
    auto parseHandYArray = [&](const string &key, vector<float> &outYs)
    {
        size_t pos = json.find(key);
        if (pos == string::npos)
            return;

        size_t arrStart = json.find("[", pos);
        size_t arrEnd = json.find("]", arrStart);
        if (arrStart == string::npos || arrEnd == string::npos)
            return;

        string arr = json.substr(arrStart + 1, arrEnd - arrStart - 1);
        size_t p = 0;

        while (true)
        {
            size_t xKey = arr.find("\"x\"", p);
            if (xKey == string::npos)
                break;

            size_t xColon = arr.find(":", xKey);
            size_t xEnd = arr.find_first_of(",}", xColon);

            size_t yKey = arr.find("\"y\"", xEnd);
            if (yKey == string::npos)
                break;

            size_t yColon = arr.find(":", yKey);
            size_t yEnd = arr.find_first_of(",}", yColon);
            float y = std::stof(arr.substr(yColon + 1, yEnd - (yColon + 1)));

            outYs.push_back(y);
            p = yEnd;
        }
    };

    g_handAnchorsPlrY.clear();
    g_handAnchorsBotY.clear();
    parseHandYArray("\"player_hand\"", g_handAnchorsPlrY);
    parseHandYArray("\"bot_hand\"", g_handAnchorsBotY);

    // ----------------------------
    // Парсим attackSlots и defendSlots
    // ----------------------------
    g_attackSlots.clear();
    g_defendSlots.clear();
    parseXYArray("\"attack_slots\"", g_attackSlots, true);
    parseXYArray("\"defense_slots\"", g_defendSlots, false);

    // center_y = среднее Y слотов
    if (!g_attackSlots.empty())
    {
        float sumY = 0.f;
        for (auto &s : g_attackSlots)
            sumY += s.pos.y;
        L.center_y = sumY / g_attackSlots.size();
    }
    else if (!g_defendSlots.empty())
    {
        float sumY = 0.f;
        for (auto &s : g_defendSlots)
            sumY += s.pos.y;
        L.center_y = sumY / g_defendSlots.size();
    }

    return L;
}

static void load_start_screen_layout()
{
    g_scLayout.titlePos = {960.f, 120.f};
    g_scLayout.titleSize = 80;
    g_scLayout.logoPos = {1650.f, 820.f};
    g_scLayout.logoScale = 0.45f;
    g_scLayout.menuPos = {
        {960.f, 380.f}, {960.f, 460.f}, {960.f, 540.f},
        {960.f, 620.f}, {960.f, 700.f}
    };
    g_scLayout.cardsPos = {
        {150.f, 280.f}, {220.f, 280.f}, {150.f, 460.f},
        {220.f, 460.f}, {150.f, 640.f}, {220.f, 640.f}
    };

    ifstream f("layout_scenes.json");
    if (!f.is_open())
    {
        cout << "NO JSON layout_scenes.json\n";
        return;
    }

    string json((istreambuf_iterator<char>(f)), istreambuf_iterator<char>());

    auto getVal = [&](const string &key, size_t start) -> float
    {
        size_t p = json.find(key, start);
        if (p == string::npos) return 0;
        p = json.find(":", p);
        size_t end = json.find_first_of(",}", p + 1);
        return stof(json.substr(p + 1, end - (p + 1)));
    };

    // start_screen section
    size_t ss = json.find("\"start_screen\"");
    if (ss == string::npos) return;

    // title
    size_t t = json.find("\"title\"", ss);
    if (t != string::npos)
    {
        g_scLayout.titlePos.x = getVal("\"x\"", t);
        g_scLayout.titlePos.y = getVal("\"y\"", t);
        g_scLayout.titleSize = (int)getVal("\"size\"", t);
    }

    // logo
    size_t l = json.find("\"logo\"", ss);
    if (l != string::npos)
    {
        g_scLayout.logoFile = "emotion/logo_start_game3.png";
        g_scLayout.logoPos.x = getVal("\"x\"", l);
        g_scLayout.logoPos.y = getVal("\"y\"", l);
        g_scLayout.logoScale = getVal("\"scale\"", l);
    }

    // menu items
    size_t m = json.find("\"menu\"", ss);
    if (m != string::npos)
    {
        size_t arrStart = json.find("[", m);
        size_t arrEnd = json.find("]", arrStart);
        if (arrStart != string::npos && arrEnd != string::npos)
        {
            string arr = json.substr(arrStart + 1, arrEnd - arrStart - 1);
            g_scLayout.menuPos.clear();
            size_t p = 0;
            while (true)
            {
                size_t xKey = arr.find("\"x\"", p);
                if (xKey == string::npos) break;
                float xv = getVal("\"x\"", arrStart + p);
                float yv = getVal("\"y\"", arrStart + p);
                g_scLayout.menuPos.push_back({xv, yv});
                p = arr.find("}", xKey) + 1;
            }
        }
    }

    // cards
    size_t c = json.find("\"cards\"", ss);
    if (c != string::npos)
    {
        size_t arrStart = json.find("[", c);
        size_t arrEnd = json.find("]", arrStart);
        if (arrStart != string::npos && arrEnd != string::npos)
        {
            string arr = json.substr(arrStart + 1, arrEnd - arrStart - 1);
            g_scLayout.cardsPos.clear();
            size_t p = 0;
            while (true)
            {
                size_t xKey = arr.find("\"x\"", p);
                if (xKey == string::npos) break;
                float xv = getVal("\"x\"", arrStart + p);
                float yv = getVal("\"y\"", arrStart + p);
                g_scLayout.cardsPos.push_back({xv, yv});
                p = arr.find("}", xKey) + 1;
            }
        }
    }
}

// ------------------------------------------------------------
// HAND LAYOUT
// ------------------------------------------------------------
static void layout_hand(
    vector<CardVisual> &hand,
    float centerX,
    float y,
    const vector<float> &anchorsY)
{
    const float cardWidth = 124.f;
    const float gap = 10.f;
    const float minOverlap = 5.f;
    const float W = 1240.f;

    int N = (int)hand.size();
    if (N == 0)
        return;

    float step;
    if (N <= 10)
    {
        step = cardWidth + gap;
    }
    else
    {
        step = W / (N - 1);
        step = std::min(step, cardWidth + gap);
        step = std::max(step, minOverlap);
    }

    float totalWidth = step * (N - 1);
    float startX = centerX - totalWidth / 2.f;

    float baseY = y;
    if (!anchorsY.empty())
        baseY = anchorsY[0];

    for (int i = 0; i < N; ++i)
    {
        hand[i].targetPos.x = startX + i * step;
        hand[i].targetPos.y = baseY;
    }

    // синхронизируем currentPos только для уже лежащих карт
    for (auto &v : hand)
    {
        if (!v.animating && v.state == InHand)
            v.currentPos = v.targetPos;
    }
}
// ------------------------------------------------------------
// ANIMATION: easeOutQuad (если понадобится)
// ------------------------------------------------------------
static float easeOutQuad(float t)
{
    return 1.f - (1.f - t) * (1.f - t);
}

// ------------------------------------------------------------
// СОРТИРОВКА КАРТ В РУКЕ
// ------------------------------------------------------------
static void sort_hand(
    vector<CardVisual> &hand,
    float centerX,
    float y,
    const vector<float> &anchorsY)
{
    if (hand.size() <= 1)
        return;

    const std::string &mode = g_settings.card_sort_mode;
    const std::string &dir = g_settings.card_sort_direction;
    const std::string &trumpPos = g_settings.card_sort_trump_position;
    int trump = g_trumpSuit;

    std::sort(hand.begin(), hand.end(), [&](const CardVisual &a, const CardVisual &b)
    {
        bool aTrump = (a.card.suit == trump);
        bool bTrump = (b.card.suit == trump);

        // 1. Trump position
        if (aTrump != bTrump && trumpPos != "none")
        {
            if (trumpPos == "left")  return aTrump;  // козыри слева
            if (trumpPos == "right") return bTrump;  // козыри справа
        }

        // 2. Сортировка внутри группы
        bool asc = (dir == "asc");

        if (mode == "suit")
        {
            if (a.card.suit != b.card.suit)
                return asc ? a.card.suit < b.card.suit : a.card.suit > b.card.suit;
            return asc ? a.card.rank < b.card.rank : a.card.rank > b.card.rank;
        }

        // mode == "rank"
        if (a.card.rank != b.card.rank)
            return asc ? a.card.rank < b.card.rank : a.card.rank > b.card.rank;
        return asc ? a.card.suit < b.card.suit : a.card.suit > b.card.suit;
    });

    // Пересчитать позиции после сортировки
    layout_hand(hand, centerX, y, anchorsY);

    // Обновить currentPos для карт, которые уже в руке (не анимируются)
    for (auto &v : hand)
    {
        if (!v.animating && v.state == InHand)
        {
            v.currentPos = v.targetPos;
            v.sprite.setPosition(
                v.currentPos.x + v.shakeOffset,
                v.currentPos.y + v.liftOffset);
        }
    }
}

// ------------------------------------------------------------
// ANIMATE CARDS
// ------------------------------------------------------------
static void animate_cards(
    vector<CardVisual> &hand,
    sf::Texture cardTex[4][9],
    const sf::Texture &texBack,
    const Layout &L)
{ //  скорость анимации
    const float speed = 30.f;

    for (auto it = hand.begin(); it != hand.end(); /* in-loop */)
    {
        CardVisual &v = *it;

        // карта не анимируется: просто рисуем по currentPos + эффекты
        if (!v.animating)
        {
            v.sprite.setPosition(
                v.currentPos.x + v.shakeOffset,
                v.currentPos.y + v.liftOffset);
            ++it;
            continue;
        }

        sf::Vector2f dir = v.targetPos - v.currentPos;
        float dist = std::sqrt(dir.x * dir.x + dir.y * dir.y);

        // вычисляем начальную дистанцию для flip в полёте
        if (v.flipMidFlight && v.initialDist == 0.f)
        {
            v.initialDist = dist;
        }

        if (dist > 1.f)
        {
            dir /= dist;
            float step = std::min(speed, dist);
            v.currentPos += dir * step;

            // позиция во время полёта — БЕЗ shake/lift
            v.sprite.setPosition(v.currentPos);

            // отбой
            if (v.state == MovingToDiscard)
            {
                v.sprite.setRotation(v.sprite.getRotation() + v.rotSpeed * 0.1f);

                v.alpha -= 5.f;
                if (v.alpha < 0.f)
                    v.alpha = 0.f;
                v.sprite.setColor(sf::Color(255, 255, 255, (sf::Uint8)v.alpha));
            }

            // mid-flight flip
            if (v.flipMidFlight && v.initialDist > 0.f && dist < v.initialDist * 0.5f)
            {
                v.sprite.setTexture(cardTex[v.card.suit][v.card.rank - 6]);
                v.flipMidFlight = false;
            }

            ++it;
        }
        else
        {
            // приземление
            if (v.state == MovingToDiscard)
            {
                it = hand.erase(it);
                continue;
            }

            v.currentPos.x = std::round(v.targetPos.x);
            v.currentPos.y = std::round(v.targetPos.y);

            // карта стоит → применяем shake/lift
            v.sprite.setPosition(
                v.currentPos.x + v.shakeOffset,
                v.currentPos.y + v.liftOffset);

            v.animating = false;

            if (v.state == MovingToSlot)
            {
                v.state = InSlot;
                g_tableVisuals.push_back(v);
                it = hand.erase(it);

                if (v.owner == PLR)
                    layout_hand(hand, L.center_x, L.plr_y, g_handAnchorsPlrY);
                else
                    layout_hand(hand, L.center_x, L.bot_y, g_handAnchorsBotY);
            }
            else
            {
                v.state = InHand;

                // не сортируем сразу, а выставляем флаг отложенной сортировки
                if (v.owner == PLR)
                    g_pendingSortPlr = true;
                else
                    g_pendingSortBot = true;

                ++it;
            }
        }
    }
}

// ------------------------------------------------------------
// CARD → STRING (для hover и выбора)
// ------------------------------------------------------------
static std::string ux_card_to_string(const Card &c)
{
    std::string r;
    if (c.rank == 11)
        r = "J";
    else if (c.rank == 12)
        r = "Q";
    else if (c.rank == 13)
        r = "K";
    else if (c.rank == 14)
        r = "A";
    else
        r = std::to_string(c.rank);

    char s = "cdhs"[c.suit];
    return r + s;
}

// ------------------------------------------------------------
// HOVER EFFECTS (поднятие карты)
// ------------------------------------------------------------
static void update_hover_effects(
    std::vector<CardVisual> &hand,
    const std::vector<std::string> &validMoves,
    sf::Vector2f mousePos)
{
    // Сбрасываем все карты
    for (auto &cv : hand)
    {
        if (cv.animating)
            continue;

        cv.hovered = false;
        cv.liftOffset = 0.f;
    }

    // Находим карту, на которую попал курсор
    // Если таких несколько — выбираем ПРАВУЮ (с большим индексом)
    int hoveredIndex = -1;

    for (int i = 0; i < (int)hand.size(); i++)
    {
        auto &cv = hand[i];

        if (cv.animating)
            continue;

        // Вычисляем hover-область на основе БАЗОВОЙ позиции карты (currentPos),
        // а не позиции спрайта. Это критически важно, потому что:
        // 1. update_hover_effects вызывается ДО animate_cards
        // 2. Позиция спрайта ещё содержит старый liftOffset от предыдущего кадра
        // 3. Если использовать getGlobalBounds(), hoverBounds будет смещён
        //    относительно реальной позиции карты, что вызывает "дергание"
        //
        // Ориджин карты: (62, 90), размер текстуры: 124x180
        // Левый верхний угол карты в мировых координатах:
        float cardLeft = cv.currentPos.x - 62.f;
        float cardTop = cv.currentPos.y - 90.f;
        float cardWidth = 124.f;
        float cardHeight = 180.f;

        // Hover-область сдвинута вверх на 10px (величина подъёма карты).
        // Карта поднимается только когда курсор заехал выше нижней границы
        // на величину hover-а (10px). Это предотвращает ситуацию, когда
        // курсор на нижней границе вызывает подъём карты и сброс hover-а.
        sf::FloatRect hoverBounds(
            cardLeft,
            cardTop - 10.f, // сдвигаем всю область вверх на 10px
            cardWidth,
            cardHeight // высота остаётся прежней
        );

        if (hoverBounds.contains(mousePos))
        {
            hoveredIndex = i; // запоминаем последнюю (правую) карту
        }
    }

    // Поднимаем только одну выбранную карту
    if (hoveredIndex >= 0)
    {
        auto &cv = hand[hoveredIndex];
        std::string s = ux_card_to_string(cv.card);

        bool isValid = false;
        for (auto &m : validMoves)
            if (m == s)
                isValid = true;

        if (isValid && g_settings.card_hint_enabled)
        {
            cv.hovered = true;
            cv.liftOffset = -10.f;
        }
    }
}

// ------------------------------------------------------------
// LIFT + SHAKE EFFECTS
// ------------------------------------------------------------
static void update_card_effects(std::vector<CardVisual> &hand, float dt)
{
    for (auto &cv : hand)
    {
        if (cv.animating)
            continue;

        // LIFT
        if (cv.lifting)
        {
            cv.liftTimer += dt;
            float t = cv.liftTimer;

            if (t < 0.1f)
                cv.liftOffset = -10.f * (t / 0.1f);
            else if (t < 0.2f)
                cv.liftOffset = -10.f * (1.f - (t - 0.1f) / 0.1f);
            else
            {
                cv.liftOffset = 0.f;
                cv.lifting = false;
            }
        }

        // SHAKE
        if (cv.shaking)
        {
            cv.shakeTimer += dt;
            float t = cv.shakeTimer;

            if (t < 0.05f)
                cv.shakeOffset = 5.f;
            else if (t < 0.10f)
                cv.shakeOffset = -5.f;
            else if (t < 0.15f)
                cv.shakeOffset = 3.f;
            else if (t < 0.20f)
                cv.shakeOffset = -3.f;
            else
            {
                cv.shakeOffset = 0.f;
                cv.shaking = false;
            }
        }
    }
}
// ------------------------------------------------------------
// ADD CARD TO HAND (DECK -> HAND)
// ------------------------------------------------------------
static void add_card_to_hand(
    vector<CardVisual> &hand,
    const Card &c,
    sf::Texture cardTex[4][9],
    const sf::Texture &texBack,
    const sf::Vector2f &deckPos,
    float centerX,
    float y,
    bool faceUp,
    Side owner)
{
    // CardVisual v;
    CardVisual v{};
    v.card = c;

    // Игрок: начинаем с рубашки, потом flip в полёте
    // Бот: всегда рубашкой вверх
    if (owner == PLR && faceUp)
    {
        v.sprite.setTexture(texBack); // рубашкой вверх
        v.flipMidFlight = true;       // перевернуть в полёте
    }
    else if (owner == PLR && !faceUp)
    {
        v.sprite.setTexture(texBack);
        v.flipMidFlight = false;
    }
    else // BOT
    {
        v.sprite.setTexture(texBack);
        v.flipMidFlight = false;
    }

    v.sprite.setOrigin(62, 90);

    v.currentPos = deckPos;
    v.targetPos = deckPos;
    v.sprite.setPosition(deckPos);

    v.animating = true;
    v.state = InHand;
    v.owner = owner;

    // вычисляем дистанцию для flip
    v.initialDist = 0.f; // будет пересчитано в animate_cards()

    hand.push_back(v);

    // пересчитать цели для всех карт руки
    if (owner == PLR)
        layout_hand(hand, centerX, y, g_handAnchorsPlrY);
    else
        layout_hand(hand, centerX, y, g_handAnchorsBotY);
}

// ------------------------------------------------------------
// PLAY CARD TO SLOT (HAND -> SLOT)
// ------------------------------------------------------------
static void play_card_to_slot(
    vector<CardVisual> &hand,
    int handIndex,
    vector<Slot> &slots,
    int slotIndex,
    int pairIndex,
    bool faceUpForBot,
    sf::Texture cardTex[4][9],
    const sf::Texture &texBack)
{
    if (handIndex < 0 || handIndex >= (int)hand.size())
        return;
    if (slotIndex < 0 || slotIndex >= (int)slots.size())
        return;

    Slot &s = slots[slotIndex];
    if (s.occupied)
        return;

    CardVisual &v = hand[handIndex];

    // целевая позиция — слот
    v.targetPos = s.pos;
    v.initialDist = std::sqrt(
        (v.targetPos.x - v.currentPos.x) * (v.targetPos.x - v.currentPos.x) +
        (v.targetPos.y - v.currentPos.y) * (v.targetPos.y - v.currentPos.y));

    // запуск анимации
    v.delay = 0.f;
    v.animating = true;
    v.state = MovingToSlot;
    v.tablePairIndex = pairIndex;

    // бот: показать лицом в полёте
    v.flipMidFlight = (v.owner == BOT && faceUpForBot);

    // помечаем слот
    s.occupied = true;
    s.pairIndex = pairIndex;
    s.faceUp = faceUpForBot;
}

// ------------------------------------------------------------
// START DISCARD ANIMATION (TABLE -> DISCARD)
// ------------------------------------------------------------
static void start_discard_animation(const Layout &L)
{
    // 0. Воспроизводим звук "Бито!"
    play_sound(g_soundBito);

    // 1. Все карты на столе отправляем в отбой
    for (auto &v : g_tableVisuals)
    {
        v.state = MovingToDiscard;
        v.animating = true;

        // цель — точка отбоя
        v.targetPos = {-200.f, L.center_y};

        // случайный поворот
        v.rotSpeed = (rand() % 41 - 20); // -20..20 градусов

        // включаем fade-out
        v.alpha = 255.f;
        v.sprite.setColor(sf::Color(255, 255, 255, 255));
    }

    // 2. Очищаем слоты
    for (auto &s : g_attackSlots)
    {
        s.occupied = false;
        s.pairIndex = -1;
    }
    for (auto &s : g_defendSlots)
    {
        s.occupied = false;
        s.pairIndex = -1;
    }
}

// ------------------------------------------------------------
// TABLE -> HAND (сбор стола в руку)
// ------------------------------------------------------------
static void start_table_to_hand(
    Side taker,
    vector<CardVisual> &vis_plr,
    vector<CardVisual> &vis_bot,
    sf::Texture cardTex[4][9],
    const sf::Texture &texBack,
    const Layout &L)
{
    vector<CardVisual> &hand = (taker == PLR ? vis_plr : vis_bot);

    for (auto &v : g_tableVisuals)
    {
        CardVisual nv;
        nv.card = v.card;

        bool faceUp = (taker == PLR);
        if (faceUp)
            nv.sprite.setTexture(cardTex[v.card.suit][v.card.rank - 6]);
        else
            nv.sprite.setTexture(texBack);

        nv.sprite.setOrigin(62, 90);

        // стартовая позиция — там, где карта лежит на столе
        nv.currentPos = v.currentPos;
        nv.sprite.setPosition(nv.currentPos);

        // временная targetPos = currentPos (layout_hand потом поправит)
        nv.targetPos = nv.currentPos;

        nv.animating = true;
        nv.state = InHand;
        nv.owner = taker;
        nv.flipMidFlight = false;
        nv.initialDist = 0.f;

        hand.push_back(nv);
    }

    // очищаем стол
    g_tableVisuals.clear();

    for (auto &s : g_attackSlots)
    {
        s.occupied = false;
        s.pairIndex = -1;
    }
    for (auto &s : g_defendSlots)
    {
        s.occupied = false;
        s.pairIndex = -1;
    }

    // пересчитать раскладку руки
    if (taker == PLR)
        layout_hand(vis_plr, L.center_x, L.plr_y, g_handAnchorsPlrY);
    else
        layout_hand(vis_bot, L.center_x, L.bot_y, g_handAnchorsBotY);
}
// ------------------------------------------------------------
// FIND MOVE INDEX BY STRING
// ------------------------------------------------------------
static int ux_find_move_index_for_string(const std::string &s)
{
    if (!g_pending.active)
        return -1;

    for (int i = 0; i < (int)g_pending.moves.size(); ++i)
        if (g_pending.moves[i] == s)
            return i;

    return -1;
}

static int ux_find_pass_index()
{
    return ux_find_move_index_for_string("p");
}

// ------------------------------------------------------------
// HANDLE EVENTS (клики, hover, PASS)
// ------------------------------------------------------------
static void ux_handle_events()
{
    sf::Event e;
    while (g_window && g_window->pollEvent(e))
    {
        sf::Vector2i mpix = sf::Mouse::getPosition(*g_window);
        sf::Vector2f mp = g_window->mapPixelToCoords(mpix);

        // hover по картам игрока
        if (g_uxMode == UxMode::WaitPlayerMove && g_pending.active)
        {
            update_hover_effects(g_vis_plr, g_pending.moves, mp);
        }

        // закрытие окна (крестик)
        if (e.type == sf::Event::Closed)
        {
            g_shouldExit = true;
        }

        // обработка ESC
        if (e.type == sf::Event::KeyPressed &&
            e.key.code == sf::Keyboard::Escape)
        {
            if (g_uxMode == UxMode::StartScreen ||
                g_uxMode == UxMode::Settings ||
                g_uxMode == UxMode::Rules ||
                g_uxMode == UxMode::Authors)
            {
                // ESC на StartScreen → выход
                if (g_uxMode == UxMode::StartScreen)
                {
                    g_shouldExit = true;
                }
                else
                {
                    // Settings/Rules/Authors → назад в StartScreen
                    g_uxMode = UxMode::StartScreen;
                }
            }
            else if (g_uxMode == UxMode::GameOver)
            {
                // ESC на GameOver — игнорируем (игрок жмёт Continue)
            }
            else
            {
                // ESC на Idle/WaitPlayerMove → StartScreen (сохраняем режим)
                g_scPrevUxMode = g_uxMode;
                g_uxMode = UxMode::StartScreen;
            }
        }

        // Клик мышью
        if (e.type == sf::Event::MouseButtonPressed &&
            e.mouseButton.button == sf::Mouse::Left)
        {
            sf::Vector2f mp(e.mouseButton.x, e.mouseButton.y);

            if (g_uxMode == UxMode::WaitPlayerMove && g_pending.active)
            {
                // --- выбор карты ---
                // Находим ПРАВУЮ карту под курсором (как в ховере)
                int clickedIndex = -1;

                for (size_t i = 0; i < g_vis_plr.size(); ++i)
                {
                    if (g_vis_plr[i].sprite.getGlobalBounds().contains(mp))
                    {
                        clickedIndex = i; // запоминаем последнюю (правую) карту
                    }
                }

                // Обрабатываем клик по найденной карте
                if (clickedIndex >= 0)
                {
                    std::string s = ux_card_to_string(g_vis_plr[clickedIndex].card);
                    int idx = ux_find_move_index_for_string(s);

                    if (idx >= 0)
                    {
                        // допустимый ход
                        std::cout << "[UX] CHOSEN CARD " << s
                                  << " (move idx = " << idx << ")\n";

                        // скрываем подсказку
                        hide_hint_text();

                        g_pending.active = false;
                        g_uxMode = UxMode::Idle;
                        g_chosenMove = idx;
                        g_moveReady = true;
                    }
                    else
                    {
                        // недопустимый ход → shake
                        std::cout << "[UX] INVALID CARD " << s << "\n";
                        g_vis_plr[clickedIndex].shaking = true;
                        g_vis_plr[clickedIndex].shakeTimer = 0.f;
                    }
                }

                // --- PASS-кнопка ---
                if (g_sprAction.getColor().a > 0 &&
                    g_sprAction.getGlobalBounds().contains(mp))
                {
                    int idx = ux_find_pass_index();
                    if (idx >= 0)
                    {
                        std::cout << "[UX] CHOSEN PASS (idx = " << idx << ")\n";
                        g_pending.active = false;
                        g_uxMode = UxMode::Idle;
                        g_chosenMove = idx;
                        g_moveReady = true;

                        // запуск анимации затухания
                        g_actionButtonFading = true;
                    }
                    else
                    {
                        std::cout << "[UX] PASS not allowed\n";
                    }
                }
            }
            // game over click
            else if (g_uxMode == UxMode::GameOver)
            {
                if (g_sprAction.getColor().a > 0 &&
                    g_sprAction.getGlobalBounds().contains(mp))
                {
                    // запуск анимации затухания
                    g_actionButtonFading = true;
                    g_gameOverContinueClicked = true;

                    // останавливаем салют
                    if (g_fireworks)
                    {
                        g_fireworks->stop();
                        g_gameOverWithFireworks = false;
                    }
                }
            }
            // клик на стартовой сцене
            else if (g_uxMode == UxMode::StartScreen)
            {
                if (g_scCanResume)
                {
                    // in-game: Resume(0), Back(4)
                    int indices[] = {0, 4};
                    for (int i = 0; i < SC_RESUME_COUNT; i++)
                    {
                        int idx = indices[i];
                        if (idx >= (int)g_scLayout.menuPos.size())
                            idx = (int)g_scLayout.menuPos.size() - 1;
                        g_scMenuText.setString(SC_RESUME_ITEMS[i]);
                        auto b = g_scMenuText.getLocalBounds();
                        g_scMenuText.setOrigin(b.width / 2.f, b.height / 2.f);
                        g_scMenuText.setPosition(g_scLayout.menuPos[idx]);

                        if (g_scMenuText.getGlobalBounds().contains(mp))
                        {
                            if (i == 0) // Вернуться к игре
                            {
                                g_uxMode = g_scPrevUxMode;
                                g_startScreenResult = ScResult::Resume;
                            }
                            else if (i == 1) // Вернуться в основное меню
                            {
                                g_startScreenResult = ScResult::Back;
                            }
                            break;
                        }
                    }
                }
                else
                {
                    // главное меню: NewGame(1), Settings(2), Rules(3), Authors(4)
                    for (int i = 0; i < SC_MAIN_COUNT; i++)
                    {
                        int idx = i + 1;
                        if (idx >= (int)g_scLayout.menuPos.size())
                            idx = (int)g_scLayout.menuPos.size() - 1;
                        g_scMenuText.setString(SC_MAIN_ITEMS[i]);
                        auto b = g_scMenuText.getLocalBounds();
                        g_scMenuText.setOrigin(b.width / 2.f, b.height / 2.f);
                        g_scMenuText.setPosition(g_scLayout.menuPos[idx]);

                        if (g_scMenuText.getGlobalBounds().contains(mp))
                        {
                            if (i == 0) // Начать новую партию
                            {
                                g_startScreenResult = ScResult::NewGame;
                            }
                            else if (i == 1) // Настройки
                            {
                                g_uxMode = UxMode::Settings;
                            }
                            else if (i == 2) // Правила игры
                            {
                                g_uxMode = UxMode::Rules;
                            }
                            else if (i == 3) // Об авторах
                            {
                                g_uxMode = UxMode::Authors;
                            }
                            break;
                        }
                    }
                }
            }
            // клик на сцене настроек
            else if (g_uxMode == UxMode::Settings)
            {
                // пока просто возврат на StartScreen
                g_uxMode = UxMode::StartScreen;
            }
            // клик на сцене правил
            else if (g_uxMode == UxMode::Rules)
            {
                g_uxMode = UxMode::StartScreen;
            }
            // клик на сцене об авторах
            else if (g_uxMode == UxMode::Authors)
            {
                g_uxMode = UxMode::StartScreen;
            }
        }
    }
}

// ------------------------------------------------------------
// UX API (публичные функции для движка)
// ------------------------------------------------------------

void ux_start_wait_player_move(const std::vector<std::string> &moves)
{
    g_pending.moves = moves;
    g_pending.active = true;
    g_uxMode = UxMode::WaitPlayerMove;

    g_moveReady = false;
    g_chosenMove = -1;

    std::cout << "[UX] Waiting for player move, moves = ";
    for (auto &m : moves)
        std::cout << m << " ";
    std::cout << "\n";
}

bool ux_move_ready()
{
    return g_moveReady;
}

int ux_get_chosen_move()
{
    return g_chosenMove;
}
// ------------------------------------------------------------
// RUN_COMMAND — визуальное применение команды от движка
// ------------------------------------------------------------

void ux_cmd(const std::string &name,
            const std::vector<std::string> &args)
{
    g_cmdQueue.push(UxCommand{name, args});
}

void ux_run_command(const UxCommand &cmd)
{
    std::cout << "[UX CMD] " << cmd.name;
    for (auto &a : cmd.args)
        std::cout << " " << a;
    std::cout << "\n";

    if (cmd.name == "WAIT")
    {
        float ms = std::stof(cmd.args[0]);
        g_waitTimer = ms / 1000.f;
        return;
    }

    if (cmd.name == "DEAL_CARD")
    {
        play_sound(g_soundFromKol);

        Side s = (cmd.args[0] == "PLR" ? PLR : BOT);
        Card c = parse_card(cmd.args[1]);
        bool faceUp = (s == PLR);

        if (s == PLR)
            add_card_to_hand(g_vis_plr, c, g_cardTex, g_texBack,
                             g_layout.deck_pos, g_layout.center_x, g_layout.plr_y,
                             faceUp, PLR);
        else
            add_card_to_hand(g_vis_bot, c, g_cardTex, g_texBack,
                             g_layout.deck_pos, g_layout.center_x, g_layout.bot_y,
                             faceUp, BOT);
    }

    else if (cmd.name == "SET_TRUMP")
    {
        Card c = parse_card(cmd.args[0]);
        g_trumpSuit = c.suit;
        g_trumpSpr.setTexture(g_cardTex[c.suit][c.rank - 6]);
        g_trumpSpr.setColor(sf::Color(255, 255, 255, 255)); // мгновенно видимый
        g_trumpVisible = true;
    }

    else if (cmd.name == "UPDATE_DECK_SIZE")
    {
        int n = std::stoi(cmd.args[0]);
        g_deckSize = n;
        g_deckVisible = (n > 1);

        // обновляем текст количества карт в колоде
        g_deckCountText.setString(L"в колоде:" + std::to_wstring(n));
    }

    else if (cmd.name == "UPDATE_EMOTION")
    {
        std::string s = cmd.args[0];
        if (s == "JOY" || s == "JOY (1)")
            g_sprEmotion.setTexture(g_texEmotion[0]);
        else if (s == "SADNESS" || s == "SADNESS (-1)")
            g_sprEmotion.setTexture(g_texEmotion[2]);
        else
            g_sprEmotion.setTexture(g_texEmotion[1]);
    }

    else if (cmd.name == "TABLE_TO_HAND")
    {
        play_sound(g_soundTake);

        Side taker = (cmd.args[0] == "PLR" ? PLR : BOT);

        start_table_to_hand(
            taker,
            g_vis_plr,
            g_vis_bot,
            g_cardTex,
            g_texBack,
            g_layout);
    }

    else if (cmd.name == "PLAY_KOLODA_SOUND")
    {
        play_sound(g_soundKoloda);
    }

    else if (cmd.name == "PLAY_ATTACK")
    {
        play_sound(g_soundFallDown);

        Side s = (cmd.args[0] == "PLR" ? PLR : BOT);
        Card c = parse_card(cmd.args[1]);
        int slotIndex = std::stoi(cmd.args[2]);

        vector<CardVisual> &hand = (s == PLR ? g_vis_plr : g_vis_bot);
        int handIndex = -1;

        for (int i = 0; i < (int)hand.size(); i++)
            if (hand[i].card.rank == c.rank && hand[i].card.suit == c.suit)
                handIndex = i;

        if (handIndex != -1)
        {
            play_card_to_slot(
                hand,
                handIndex,
                g_attackSlots,
                slotIndex,
                slotIndex,
                true,
                g_cardTex,
                g_texBack);
        }
    }

    else if (cmd.name == "PLAY_DEFENSE")
    {
        play_sound(g_soundFallDown);

        Side s = (cmd.args[0] == "PLR" ? PLR : BOT);
        Card c = parse_card(cmd.args[1]);
        int slotIndex = std::stoi(cmd.args[2]);

        vector<CardVisual> &hand = (s == PLR ? g_vis_plr : g_vis_bot);
        int handIndex = -1;

        for (int i = 0; i < (int)hand.size(); i++)
            if (hand[i].card.rank == c.rank && hand[i].card.suit == c.suit)
                handIndex = i;

        if (handIndex != -1)
        {
            play_card_to_slot(
                hand,
                handIndex,
                g_defendSlots,
                slotIndex,
                slotIndex,
                true,
                g_cardTex,
                g_texBack);
        }
    }

    else if (cmd.name == "CLEAR_TABLE")
    {
        start_discard_animation(g_layout);
    }

    else if (cmd.name == "DEAL_PREVOISE_TRUMP")
    {
        play_sound(g_soundFromKol);

        Side s = (cmd.args[0] == "PLR" ? PLR : BOT);
        Card c = parse_card(cmd.args[1]);
        bool faceUp = (s == PLR);

        if (s == PLR)
            add_card_to_hand(g_vis_plr, c, g_cardTex, g_texBack,
                             g_layout.deck_pos, g_layout.center_x, g_layout.plr_y,
                             faceUp, PLR);
        else
            add_card_to_hand(g_vis_bot, c, g_cardTex, g_texBack,
                             g_layout.deck_pos, g_layout.center_x, g_layout.bot_y,
                             faceUp, BOT);

        g_deckVisible = false;
    }

    else if (cmd.name == "DEAL_LAST_TRUMP")
    {
        play_sound(g_soundFromKol);

        Side s = (cmd.args[0] == "PLR" ? PLR : BOT);
        Card c = parse_card(cmd.args[1]);
        bool faceUp = (s == PLR);

        if (s == PLR)
            add_card_to_hand(g_vis_plr, c, g_cardTex, g_texBack,
                             g_layout.trump_pos, g_layout.center_x, g_layout.plr_y,
                             faceUp, PLR);
        else
            add_card_to_hand(g_vis_bot, c, g_cardTex, g_texBack,
                             g_layout.trump_pos, g_layout.center_x, g_layout.bot_y,
                             faceUp, BOT);

        g_trumpVisible = false;

        // Показываем спрайт масти козыря на месте пустой колоды
        if (g_showTrumpSuit)
        {
            // выбираем текстуру по масти
            if (c.suit == 0)
                g_sprTrumpSuit.setTexture(g_texCSuite); // трефы
            else if (c.suit == 1)
                g_sprTrumpSuit.setTexture(g_texDSuite); // бубны
            else if (c.suit == 2)
                g_sprTrumpSuit.setTexture(g_texHSuite); // червы
            else if (c.suit == 3)
                g_sprTrumpSuit.setTexture(g_texSSuite); // пики

            g_sprTrumpSuit.setColor(sf::Color(255, 255, 255, 255)); // показываем
        }
    }

    else if (cmd.name == "SET_ACTION_BUTTON")
    {
        std::cout << "[UX] SET_ACTION_BUTTON " << cmd.args[0] << "\n";

        g_actionButtonState = cmd.args[0];

        if (g_actionButtonState == "NONE")
        {
            // если кнопка ещё видима
            if (g_actionButtonAlpha > 0.f)
            {
                // если кнопка уже была видима игроку → запускаем затухание
                // если нет (первое скрытие при инициализации) → скрываем мгновенно
                if (g_actionButtonWasVisible)
                    g_actionButtonFading = true;
                else
                {
                    g_actionButtonAlpha = 0.f;
                    g_sprAction.setColor(sf::Color(255, 255, 255, 0));
                }
            }
        }
        else
        {
            // сброс флага затухания и альфы при показе новой кнопки
            g_actionButtonFading = false;
            g_actionButtonAlpha = 255.f;
            g_sprAction.setColor(sf::Color(255, 255, 255, 255));
            g_actionButtonWasVisible = true; // кнопка стала видима игроку

            if (g_actionButtonState == "TAKE")
                g_sprAction.setTexture(g_texTake);
            else if (g_actionButtonState == "BEAT")
                g_sprAction.setTexture(g_texBeat);
            else if (g_actionButtonState == "GIVE")
                g_sprAction.setTexture(g_texGive);
            else if (g_actionButtonState == "CONTINUE")
                g_sprAction.setTexture(g_texContinue);
        }
    }

    else if (cmd.name == "SHOW_HINT")
    {
        show_hint_text();
    }

    else if (cmd.name == "GAMEOVER")
    {
        const std::string &res = cmd.args[0];

        if (res == "PLR_WIN")
            g_gameOverText.setString(L"Победа");
        else if (res == "BOT_WIN")
            g_gameOverText.setString(L"Поражение");
        else
            g_gameOverText.setString(L"Ничья");

        auto b = g_gameOverText.getLocalBounds();
        g_gameOverText.setOrigin(b.width / 2.f, b.height / 2.f);
        g_gameOverText.setPosition(1920.f / 2.f, 1080.f / 2.f - 100.f);

        g_gameOverActive = true;
        g_uxMode = UxMode::GameOver;

        // записать результат в статистику
        if (res == "PLR_WIN")
            record_game_result(true);
        else if (res == "BOT_WIN")
            record_game_result(false);
        else
            record_draw(); // ничья — тоже считаем

        // показать карты бота лицом вверх
        for (auto &cv : g_vis_bot)
            cv.sprite.setTexture(g_cardTex[cv.card.suit][cv.card.rank - 6]);

        // скрыть колоду и козырь
        g_deckSpr.setColor(sf::Color(255, 255, 255, 0));
        g_trumpSpr.setColor(sf::Color(255, 255, 255, 0));

        // скрыть спрайт масти козыря
        g_sprTrumpSuit.setColor(sf::Color(255, 255, 255, 0));
        g_showTrumpSuit = false;

        // кнопка CONTINUE
        g_actionButtonState = "CONTINUE";
        g_sprAction.setTexture(g_texContinue);
        g_sprAction.setColor(sf::Color(255, 255, 255, 255));

        // запускаем салют только для победы
        if (res == "PLR_WIN" && g_fireworks)
        {
            g_gameOverWithFireworks = true;
            g_rocketSoundPlayed = false;
            g_fireworks->start();
        }
    }
}
// ------------------------------------------------------------
// PARSE CARD "6C", "10D", "QH", "AS"
// ------------------------------------------------------------
static Card parse_card(const std::string &s)
{
    Card c{0, 0};
    if (s.size() < 2)
        return c;

    std::string rankStr = s.substr(0, s.size() - 1);
    char suitCh = s.back();

    int r = 0;
    if (rankStr == "J")
        r = 11;
    else if (rankStr == "Q")
        r = 12;
    else if (rankStr == "K")
        r = 13;
    else if (rankStr == "A")
        r = 14;
    else
        r = std::stoi(rankStr);

    int sIdx = 0;
    if (suitCh == 'c' || suitCh == 'C')
        sIdx = 0;
    else if (suitCh == 'd' || suitCh == 'D')
        sIdx = 1;
    else if (suitCh == 'h' || suitCh == 'H')
        sIdx = 2;
    else if (suitCh == 's' || suitCh == 'S')
        sIdx = 3;

    c.rank = r;
    c.suit = sIdx;
    return c;
}

// ------------------------------------------------------------
// ANY ANIMATING?
// ------------------------------------------------------------
static bool anyAnimating(
    const vector<CardVisual> &a,
    const vector<CardVisual> &b,
    const vector<CardVisual> &t)
{
    auto check = [](const vector<CardVisual> &v)
    {
        for (auto &c : v)
            if (c.animating)
                return true;
        return false;
    };
    return check(a) || check(b) || check(t);
}

// ------------------------------------------------------------
// DRAW FRAME
// ------------------------------------------------------------
static void ux_draw_frame()
{
    // если выходим — НЕ рисуем ничего, только чёрный экран
    if (g_shouldExit)
    {
        g_window->clear(sf::Color::Black);
        g_window->display();
        return;
    }

    g_window->clear();

    // === ОТДЕЛЬНАЯ СЦЕНА СТАРТОВОГО МЕНЮ ===
    if (g_uxMode == UxMode::StartScreen ||
        g_uxMode == UxMode::Settings ||
        g_uxMode == UxMode::Rules ||
        g_uxMode == UxMode::Authors)
    {
        // 1. Фон
        g_window->draw(g_sprBg);

        // 2. Заголовок (только на StartScreen)
        if (g_uxMode == UxMode::StartScreen)
        {
            // 6 карт-пар слева (только на главном меню)
            if (g_scCardsInit && !g_scCanResume)
            {
                for (auto &spr : g_scCards)
                    g_window->draw(spr);
            }

            // позиция из layout
            g_scTitleText.setOrigin(0, 0);
            auto tb = g_scTitleText.getLocalBounds();
            g_scTitleText.setOrigin(tb.width / 2.f, tb.height / 2.f);
            g_scTitleText.setPosition(g_scLayout.titlePos);
            g_window->draw(g_scTitleText);

            // 3. Лого
            if (g_scLogoLoaded)
                g_window->draw(g_scLogoSprite);

            // 4. Меню из layout
            if (g_scCanResume)
            {
                // in-game: Resume (0), Back (4)
                int indices[] = {0, 4};
                for (int i = 0; i < SC_RESUME_COUNT; i++)
                {
                    int idx = indices[i];
                    if (idx >= (int)g_scLayout.menuPos.size())
                        idx = (int)g_scLayout.menuPos.size() - 1;
                    g_scMenuText.setString(SC_RESUME_ITEMS[i]);
                    auto b = g_scMenuText.getLocalBounds();
                    g_scMenuText.setOrigin(b.width / 2.f, b.height / 2.f);
                    g_scMenuText.setPosition(g_scLayout.menuPos[idx]);
                    g_window->draw(g_scMenuText);
                }
            }
            else
            {
                // главное меню: NewGame(1), Settings(2), Rules(3), Authors(4)
                for (int i = 0; i < SC_MAIN_COUNT; i++)
                {
                    int idx = i + 1;
                    if (idx >= (int)g_scLayout.menuPos.size())
                        idx = (int)g_scLayout.menuPos.size() - 1;
                    g_scMenuText.setString(SC_MAIN_ITEMS[i]);
                    auto b = g_scMenuText.getLocalBounds();
                    g_scMenuText.setOrigin(b.width / 2.f, b.height / 2.f);
                    g_scMenuText.setPosition(g_scLayout.menuPos[idx]);
                    g_window->draw(g_scMenuText);
                }
            }

            // 5. Статистика
            g_window->draw(g_statsText);
        }
        else if (g_uxMode == UxMode::Settings)
        {
            // Заглушка для настроек
            static sf::Text scSettingsText;
            scSettingsText.setFont(g_font);
            scSettingsText.setCharacterSize(48);
            scSettingsText.setFillColor(sf::Color(255, 165, 0));
            scSettingsText.setOutlineColor(sf::Color::Black);
            scSettingsText.setOutlineThickness(2.5f);
            scSettingsText.setString(L"НАСТРОЙКИ (кликните чтобы вернуться)");
            auto sb = scSettingsText.getLocalBounds();
            scSettingsText.setOrigin(sb.width / 2.f, sb.height / 2.f);
            scSettingsText.setPosition(960.f, 540.f);
            g_window->draw(scSettingsText);
        }
        else if (g_uxMode == UxMode::Rules)
        {
            static sf::Text scRulesText;
            scRulesText.setFont(g_font);
            scRulesText.setCharacterSize(48);
            scRulesText.setFillColor(sf::Color(255, 165, 0));
            scRulesText.setOutlineColor(sf::Color::Black);
            scRulesText.setOutlineThickness(2.5f);
            scRulesText.setString(L"ПРАВИЛА ИГРЫ (кликните чтобы вернуться)");
            auto rb = scRulesText.getLocalBounds();
            scRulesText.setOrigin(rb.width / 2.f, rb.height / 2.f);
            scRulesText.setPosition(960.f, 540.f);
            g_window->draw(scRulesText);
        }
        else if (g_uxMode == UxMode::Authors)
        {
            static sf::Text scAuthorsText;
            scAuthorsText.setFont(g_font);
            scAuthorsText.setCharacterSize(48);
            scAuthorsText.setFillColor(sf::Color(255, 165, 0));
            scAuthorsText.setOutlineColor(sf::Color::Black);
            scAuthorsText.setOutlineThickness(2.5f);
            scAuthorsText.setString(L"ОБ АВТОРАХ (кликните чтобы вернуться)");
            auto ab = scAuthorsText.getLocalBounds();
            scAuthorsText.setOrigin(ab.width / 2.f, ab.height / 2.f);
            scAuthorsText.setPosition(960.f, 540.f);
            g_window->draw(scAuthorsText);
        }

        g_window->display();
        return;
    }

    // === ОБЫЧНАЯ ИГРОВАЯ СЦЕНА ===
    g_window->draw(g_sprBg);

    // колода и козырь
    if (g_trumpVisible)
        g_window->draw(g_trumpSpr);
    if (g_deckVisible && g_deckSize > 1)
        g_window->draw(g_deckSpr);

    // значок козыря (когда колода пуста и козырь не виден)
    if (!g_trumpVisible && g_deckSize == 0 && g_showTrumpSuit)
    {
        g_window->draw(g_sprTrumpSuit);
    }

    // карты на столе
    for (auto &v : g_tableVisuals)
        g_window->draw(v.sprite);

    // карты в руках (статичные)
    for (auto &v : g_vis_plr)
        if (!v.animating)
            g_window->draw(v.sprite);
    for (auto &v : g_vis_bot)
        if (!v.animating)
            g_window->draw(v.sprite);

    // карты в руках (анимирующиеся)
    for (auto &v : g_vis_plr)
        if (v.animating)
            g_window->draw(v.sprite);
    for (auto &v : g_vis_bot)
        if (v.animating)
            g_window->draw(v.sprite);

    // action button
    if (g_sprAction.getColor().a > 0)
        g_window->draw(g_sprAction);

    // количество карт в колоде (всегда рисуем, если > 0)
    if (g_deckSize > 0)
        g_window->draw(g_deckCountText);

    // статистика игр (рисуем всегда)
    g_window->draw(g_statsText);

    // эмоция — картинка (левый верхний угол)
    g_window->draw(g_sprEmotion);

    // подсказка игроку (если видима)
    if (g_hintVisible || g_hintFading)
        g_window->draw(g_hintText);

    // finish him — тень + обводка для контраста
    if (g_gameOverActive)
    {
        sf::Text shadowText = g_gameOverText;
        shadowText.setFillColor(sf::Color(0, 0, 0, 180));
        shadowText.setOutlineColor(sf::Color::Transparent);
        shadowText.setOutlineThickness(0);
        shadowText.move(5.f, 5.f);
        g_window->draw(shadowText);
        g_window->draw(g_gameOverText);
    }

    // салют (рисуем поверх всего, но НЕ блокируем клики)
    if (g_gameOverWithFireworks && g_fireworks)
    {
        // рисуем салют без затемнения (чтобы клики проходили)
        g_fireworks->draw(*g_window);
    }

    g_window->display();
}

// ------------------------------------------------------------
// UX PROCESS FRAME — главный кадр UX
// ------------------------------------------------------------
void ux_process_frame()
{
    // ПРОВЕРКА: если нажат ESC — выходим немедленно
    if (g_shouldExit)
        return;

    float dt = g_clock.restart().asSeconds();

    // анимация кнопки
    if (g_actionButtonFading)
    {
        g_actionButtonAlpha -= 500.f * dt; // 500 в секунду
        if (g_actionButtonAlpha < 0.f)
            g_actionButtonAlpha = 0.f;
        g_sprAction.setColor(sf::Color(255, 255, 255, (sf::Uint8)g_actionButtonAlpha));

        if (g_actionButtonAlpha == 0.f)
        {
            g_actionButtonFading = false;
            g_actionButtonState = "NONE"; // сбрасываем состояние после затухания
        }
    }

    // анимация подсказки
    if (g_hintFading)
    {
        g_hintAlpha -= 300.f * dt; // 300 в секунду
        if (g_hintAlpha < 0.f)
            g_hintAlpha = 0.f;
        g_hintText.setFillColor(sf::Color(255, 165, 0, (sf::Uint8)g_hintAlpha));
        g_hintText.setOutlineColor(sf::Color(0, 0, 0, (sf::Uint8)g_hintAlpha));

        if (g_hintAlpha == 0.f)
        {
            g_hintFading = false;
            g_hintVisible = false;
        }
    }

    // анимация кнопки выхода
    if (g_exitButtonFading)
    {
        g_exitButtonAlpha -= 500.f * dt; // 500 в секунду
        if (g_exitButtonAlpha < 0.f)
            g_exitButtonAlpha = 0.f;
        g_sprExitContinue.setColor(sf::Color(255, 255, 255, (sf::Uint8)g_exitButtonAlpha));

        if (g_exitButtonAlpha == 0.f)
        {
            g_exitButtonFading = false;
            g_exitButtonVisible = false;
        }
    }

    // анимация сцены подтверждения выхода
    if (g_exitConfirmActive)
    {
        // если подтверждён выход — ждём 1 секунду и показываем "Пока..."
        if (g_exitConfirmed && g_exitConfirmDelayTimer > 0.f)
        {
            g_exitConfirmDelayTimer -= dt;

            // меняем текст на "Пока..."
            g_exitConfirmTextQuestion.setString(L"Пока...");
            auto bq = g_exitConfirmTextQuestion.getLocalBounds();
            g_exitConfirmTextQuestion.setOrigin(bq.width / 2.f, bq.height / 2.f);
            g_exitConfirmTextQuestion.setPosition(1920.f / 2.f, 1080.f / 2.f - 100.f);
            // скрываем инструкцию
            g_exitConfirmTextInstruction.setString(L"");

            if (g_exitConfirmDelayTimer <= 0.f)
            {
                // начинаем fade-out
                g_exitConfirmFading = true;
            }
        }
    }

    // анимация салюта (Game Over)
    if (g_gameOverWithFireworks && g_fireworks)
    {
        g_fireworks->update(dt);
    }

    // WAIT — стоп-кадр
    if (g_waitTimer > 0.f)
    {
        g_waitTimer -= dt;
        if (g_waitTimer < 0.f)
            g_waitTimer = 0.f;

        ux_handle_events();
        update_card_effects(g_vis_plr, dt);
        update_card_effects(g_vis_bot, dt);
        animate_cards(g_vis_plr, g_cardTex, g_texBack, g_layout);
        animate_cards(g_vis_bot, g_cardTex, g_texBack, g_layout);
        animate_cards(g_tableVisuals, g_cardTex, g_texBack, g_layout);
        ux_draw_frame();
        return;
    }

    // Выполняем одну команду
    // ------------------------------------------------------------
    // Выполняем команду ТОЛЬКО если нет активных анимаций
    // ------------------------------------------------------------
    if (!g_cmdQueue.empty() && !animations_active())
    {
        UxCommand cmd = g_cmdQueue.front();
        g_cmdQueue.pop();

        if (cmd.name == "WAIT")
        {
            g_waitTimer = std::stof(cmd.args[0]) / 1000.f;
        }
        else
        {
            ux_run_command(cmd);
            // Сразу отрисовываем кадр и возвращаемся — следующая команда
            // будет выполнена в следующем кадре (WAIT успеет сработать)
            ux_handle_events();
            update_card_effects(g_vis_plr, dt);
            update_card_effects(g_vis_bot, dt);
            animate_cards(g_vis_plr, g_cardTex, g_texBack, g_layout);
            animate_cards(g_vis_bot, g_cardTex, g_texBack, g_layout);
            animate_cards(g_tableVisuals, g_cardTex, g_texBack, g_layout);
            ux_draw_frame();
            return;
        }
    }

    // если все анимации завершены и есть отложенная сортировка — сортируем
    if (!animations_active() && (g_pendingSortPlr || g_pendingSortBot))
    {
        if (g_pendingSortPlr)
        {
            sort_hand(g_vis_plr, g_layout.center_x, g_layout.plr_y, g_handAnchorsPlrY);
            g_pendingSortPlr = false;
        }
        if (g_pendingSortBot)
        {
            sort_hand(g_vis_bot, g_layout.center_x, g_layout.bot_y, g_handAnchorsBotY);
            g_pendingSortBot = false;
        }
    }
    /*
      if (!g_cmdQueue.empty())
    {
        // если команда WAIT — выполняем немедленно
        if (g_cmdQueue.front().name == "WAIT")
        {
            UxCommand cmd = g_cmdQueue.front();
            g_cmdQueue.pop();
            g_waitTimer = std::stof(cmd.args[0]) / 1000.f;
        }
        // остальные команды — только если нет анимаций
        else if (!animations_active())
        {
            UxCommand cmd = g_cmdQueue.front();
            g_cmdQueue.pop();
            ux_run_command(cmd);
        }
    }  */

    ux_handle_events();
    update_card_effects(g_vis_plr, dt);
    update_card_effects(g_vis_bot, dt);
    animate_cards(g_vis_plr, g_cardTex, g_texBack, g_layout);
    animate_cards(g_vis_bot, g_cardTex, g_texBack, g_layout);
    animate_cards(g_tableVisuals, g_cardTex, g_texBack, g_layout);
    ux_draw_frame();
}

void ux_wait_gameover_continue()
{
    g_gameOverContinueClicked = false;

    while (!g_gameOverContinueClicked && g_window && g_window->isOpen())
    {
        if (g_shouldExit)
            return;
        // если ESC переключил на меню — выходим досрочно
        if (g_uxMode == UxMode::StartScreen ||
            g_uxMode == UxMode::Settings ||
            g_uxMode == UxMode::Rules ||
            g_uxMode == UxMode::Authors)
            return;
        ux_process_frame();
    }

    // -----------------------------
    // FULL UX RESET
    // -----------------------------
    g_gameOverActive = false;
    g_uxMode = UxMode::Idle;

    g_sprAction.setColor(sf::Color(255, 255, 255, 0));
    g_actionButtonState = "NONE";
    g_actionButtonWasVisible = false; // сброс для новой игры

    g_vis_plr.clear();
    g_vis_bot.clear();

    g_tableVisuals.clear();

    g_attackSlots.clear();
    g_defendSlots.clear();

    g_handAnchorsPlrY.clear();
    g_handAnchorsBotY.clear();

    g_pending.moves.clear();
    g_pending.active = false;

    g_chosenMove = -1;
    g_moveReady = false;

    g_gameOverText.setString("");

    g_trumpVisible = true;
    g_deckVisible = true;
    g_deckSize = 24;

    g_deckSpr.setColor(sf::Color(255, 255, 255, 255));
    g_trumpSpr.setColor(sf::Color(255, 255, 255, 0));

    g_showTrumpSuit = true;

    g_waitTimer = 0.f;

    // -----------------------------
    // ВОТ ЭТО — ГЛАВНОЕ
    // -----------------------------
    g_layout = load_layout(); // ← ЭТО СОЗДАЁТ СЛОТЫ ИЗ JSON
}
// ------------------------------------------------------------
// (опционально) rank_to_str — если нужно где-то ещё
// ------------------------------------------------------------
static std::string rank_to_str(int r)
{
    if (r >= 6 && r <= 10)
        return std::to_string(r);
    if (r == 11)
        return "J";
    if (r == 12)
        return "Q";
    if (r == 13)
        return "K";
    if (r == 14)
        return "A";
    return "?";
}

// ------------------------------------------------------------
// UX SHUTDOWN (пока пусто, но оставляем для чистоты API)
// ------------------------------------------------------------
void ux_shutdown()
{
    g_window = nullptr;
}

// ------------------------------------------------------------
// UX WAIT START SCREEN — блокирующий показ StartScreen
// Возвращает: Resume, NewGame или Exit
// ------------------------------------------------------------
ScResult ux_wait_start_screen(bool canResume)
{
    g_scCanResume = canResume;
    g_startScreenResult = ScResult::None;

    if (g_uxMode != UxMode::StartScreen)
    {
        g_scPrevUxMode = g_uxMode;
        g_uxMode = UxMode::StartScreen;
    }

    while (g_startScreenResult == ScResult::None && g_window && g_window->isOpen())
    {
        if (g_shouldExit)
        {
            g_startScreenResult = ScResult::Exit;
            break;
        }
        ux_process_frame();
    }

    // если new game — убеждаемся что режим Idle (для инициализации игры)
    if (g_startScreenResult != ScResult::Resume)
    {
        g_uxMode = UxMode::Idle;
    }

    return g_startScreenResult;
}

// ------------------------------------------------------------
// UX IS MENU ACTIVE — проверка что мы в каком-то меню
// ------------------------------------------------------------
bool ux_is_menu_active()
{
    return (g_uxMode == UxMode::StartScreen ||
            g_uxMode == UxMode::Settings ||
            g_uxMode == UxMode::Rules ||
            g_uxMode == UxMode::Authors);
}

// ------------------------------------------------------------
// UX RESET VISUALS — полный сброс для новой игры
// ------------------------------------------------------------
void ux_reset_visuals()
{
    reset_all_visuals();
    g_layout = load_layout();
    g_deckSpr.setPosition(g_layout.deck_pos);
    g_deckSpr.setRotation(g_layout.deck_angle);
    g_trumpSpr.setPosition(g_layout.trump_pos);
    g_trumpSpr.setRotation(g_layout.trump_angle);
    g_sprAction.setPosition(g_layout.action_pos);
}

// ------------------------------------------------------------
// WAIT UNTIL ALL ANIMATIONS FINISH
// ------------------------------------------------------------
void ux_wait_all()
{
    // крутим кадры, пока есть анимации
    while (true)
    {
        if (g_shouldExit)
            return;

        bool anim =
            anyAnimating(g_vis_plr, g_vis_bot, g_tableVisuals);

        if (!anim)
            break;

        ux_process_frame();
    }

    // один финальный кадр для стабильности
    // НО НЕ рисуем если выходим
    if (!g_shouldExit)
        ux_process_frame();
}
