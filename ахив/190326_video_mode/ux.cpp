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
    ConfirmExit

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
sf::Sprite g_sprAction;

// флаги видимости
static bool g_trumpVisible = true;
static bool g_deckVisible = true;
static int g_deckSize = 24;

// звуки
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

// текст количества карт в колоде
static sf::Text g_deckCountText;

// текст статистики игр
static sf::Text g_statsText;
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
static bool g_exitConfirmed = false;        // true если игрок нажал ESC для выхода
static float g_exitConfirmDelayTimer = 0.f; // задержка 1 сек перед "Пока..."
static float g_exitBgAlpha = 255.f;         // альфа для фона и всех элементов сцены

// значок козыря (когда колода пуста)
static sf::Text g_trumpSuitText;
static bool g_showTrumpSuit = true;          // ← легко отключить эффект
static const bool g_trumpSuitOutline = true; // ← обводка для контраста

// состояние action-кнопки
static std::string g_actionButtonState = "NONE";
static float g_actionButtonAlpha = 255.f;     // для fade-анимации
static bool g_actionButtonFading = false;     // флаг затухания
static bool g_actionButtonWasVisible = false; // была ли кнопка видима игроку

// UX режим ожидания хода
static PendingMoves g_pending;
static UxMode g_uxMode = UxMode::Idle;
static int g_chosenMove = -1;
static bool g_moveReady = false;

// таймер кадра
static sf::Clock g_clock;

// ------------------------------------------------------------
// FORWARD DECLARATIONS
// ------------------------------------------------------------
static Layout load_layout();

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

// ------------------------------------------------------------
// HELPERS FOR TRUMP SUIT SYMBOL
// ------------------------------------------------------------
static std::wstring suit_to_symbol(int suit)
{
    switch (suit)
    {
    case 0:
        return L"♣"; // трефы
    case 1:
        return L"♦"; // бубны
    case 2:
        return L"♥"; // червы
    case 3:
        return L"♠"; // пики
    }
    return L"";
}

static sf::Color suit_color(int suit)
{
    switch (suit)
    {
    case 0:
        return sf::Color(0, 0, 0); // трефы - чёрный
    case 1:
        return sf::Color(255, 0, 0); // бубны - красный
    case 2:
        return sf::Color(255, 0, 0); // червы - красный
    case 3:
        return sf::Color(0, 0, 0); // пики - чёрный
    }
    return sf::Color::White;
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

    g_trumpSuitText.setFillColor(sf::Color(255, 255, 255, 0)); // скрыть
    g_trumpSuitText.setString("");
    g_showTrumpSuit = true; // разрешить показ значка в новой игре

    g_waitTimer = 0.f;
    g_uxMode = UxMode::Idle;
    g_gameOverActive = false;

    // сброс сцены подтверждения выхода
    g_exitConfirmActive = false;
    g_exitConfirmAlpha = 255.f;
    g_exitConfirmFading = false;
    g_exitConfirmed = false;
    g_exitConfirmDelayTimer = 0.f;
    g_exitBgAlpha = 255.f;
    g_exitConfirmTextQuestion.setString(L"Покинуть игру?");
    g_exitConfirmTextInstruction.setString(L"Выход - нажмите ESC на клавиатуре,  остаться - нажмите Продолжить");

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
    g_deckCountText.setString(L"в колоде:24");
    g_deckCountText.setPosition(1802.f, 1010.f);

    // текст статистики игр
    g_statsText.setFont(g_font);
    g_statsText.setCharacterSize(20);
    g_statsText.setFillColor(sf::Color(255, 165, 0)); // оранжевый
    g_statsText.setPosition(1910.f, 1050.f);
    // выравнивание по правому краю будет в update_stats_text()

    // загружаем статистику из файла
    load_stats();
    update_stats_text();

    g_gameOverText.setFont(g_font);
    g_gameOverText.setCharacterSize(80);
    g_gameOverText.setFillColor(sf::Color(255, 165, 0)); // оранжевый
    g_gameOverText.setString("");
    g_gameOverText.setPosition(1920.f / 2.f, 1080.f / 2.f - 100.f);

    // текст подтверждения выхода
    g_exitConfirmTextQuestion.setFont(g_font);
    g_exitConfirmTextQuestion.setCharacterSize(48);
    g_exitConfirmTextQuestion.setFillColor(sf::Color(255, 165, 0)); // оранжевый
    g_exitConfirmTextQuestion.setString(L"Покинуть игру?");
    auto bq = g_exitConfirmTextQuestion.getLocalBounds();
    g_exitConfirmTextQuestion.setOrigin(bq.width / 2.f, bq.height / 2.f);
    g_exitConfirmTextQuestion.setPosition(1920.f / 2.f, 1080.f / 2.f - 150.f);

    g_exitConfirmTextInstruction.setFont(g_font);
    g_exitConfirmTextInstruction.setCharacterSize(36);
    g_exitConfirmTextInstruction.setFillColor(sf::Color(255, 165, 0)); // оранжевый
    g_exitConfirmTextInstruction.setString(L"Выход - нажмите ESC на клавиатуре,  остаться - нажмите Продолжить");
    auto bi = g_exitConfirmTextInstruction.getLocalBounds();
    g_exitConfirmTextInstruction.setOrigin(bi.width / 2.f, bi.height / 2.f);
    g_exitConfirmTextInstruction.setPosition(1920.f / 2.f, 1080.f / 2.f - 80.f);

    // текст подсказки игроку
    g_hintText.setFont(g_font);
    g_hintText.setCharacterSize(64);
    g_hintText.setFillColor(sf::Color(255, 165, 0)); // оранжевый
    g_hintText.setString(L"");
    g_hintText.setPosition(1920.f / 2.f, 1080.f / 2.f);

    // значок козыря (когда колода пуста)
    g_trumpSuitText.setFont(g_font);
    g_trumpSuitText.setCharacterSize(48); // ← увеличил с 24 до 48
    g_trumpSuitText.setString("");
    g_trumpSuitText.setFillColor(sf::Color(255, 255, 255, 0)); // скрыт по умолчанию

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

    // На старте скрываем козырь и кнопку
    g_trumpSpr.setColor(sf::Color(255, 255, 255, 0));
    g_sprAction.setColor(sf::Color(255, 255, 255, 0));
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
enum class SortMode
{
    ByRank, // по рангам (6..A)
    BySuit  // по мастям (c, d, h, s), внутри масти по рангам
};

static void sort_hand(
    vector<CardVisual> &hand,
    float centerX,
    float y,
    const vector<float> &anchorsY,
    SortMode mode)
{
    if (hand.size() <= 1)
        return;

    // Сортировка
    if (mode == SortMode::ByRank)
    {
        std::sort(hand.begin(), hand.end(), [](const CardVisual &a, const CardVisual &b)
                  {
                      if (a.card.rank != b.card.rank)
                          return a.card.rank < b.card.rank;
                      return a.card.suit < b.card.suit; // при одинаковом ранге — по масти
                  });
    }
    else if (mode == SortMode::BySuit)
    {
        std::sort(hand.begin(), hand.end(), [](const CardVisual &a, const CardVisual &b)
                  {
                      if (a.card.suit != b.card.suit)
                          return a.card.suit < b.card.suit;
                      return a.card.rank < b.card.rank; // внутри масти — по рангам
                  });
    }

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
    for (auto &cv : hand)
    {
        if (cv.animating)
            continue;

        std::string s = ux_card_to_string(cv.card);

        bool isValid = false;
        for (auto &m : validMoves)
            if (m == s)
                isValid = true;

        bool isHover = cv.sprite.getGlobalBounds().contains(mousePos);

        if (isHover && isValid)
        {
            cv.hovered = true;
            cv.liftOffset = -10.f;
        }
        else
        {
            cv.hovered = false;
            cv.liftOffset = 0.f;
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
    g_soundBito.play();

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
            // показываем сцену подтверждения выхода (как ESC)
            if (g_uxMode != UxMode::ConfirmExit)
            {
                g_uxMode = UxMode::ConfirmExit;
                g_exitConfirmActive = true;
                g_exitConfirmAlpha = 255.f;
                g_exitBgAlpha = 255.f;
                g_exitConfirmFading = false;
                g_exitConfirmed = false;
                g_exitConfirmDelayTimer = 0.f;

                // показываем кнопку "Продолжить"
                g_actionButtonState = "CONTINUE";
                g_sprAction.setTexture(g_texContinue);
                g_sprAction.setColor(sf::Color(255, 255, 255, 255));
            }
            else
            {
                // повторное нажатие на крестик — закрываемся
                FreeConsole();
                g_window->close();
            }
        }

        // обработка ESC
        if (e.type == sf::Event::KeyPressed &&
            e.key.code == sf::Keyboard::Escape)
        {
            if (g_uxMode == UxMode::ConfirmExit)
            {
                // повторное нажатие ESC — подтверждаем выход
                g_exitConfirmed = true;
                g_exitConfirmDelayTimer = 1.0f; // 1 секунда задержки
            }
            else if (g_uxMode == UxMode::GameOver)
            {
                // на Game Over сцене ESC тоже показывает подтверждение
                g_uxMode = UxMode::ConfirmExit;
                g_exitConfirmActive = true;
                g_exitConfirmAlpha = 255.f;
                g_exitBgAlpha = 255.f; // ← сброс альфа фона
                g_exitConfirmFading = false;
                g_exitConfirmed = false;
                g_exitConfirmDelayTimer = 0.f;

                // восстанавливаем текст
                g_exitConfirmTextQuestion.setString(L"Покинуть игру?");
                auto bq = g_exitConfirmTextQuestion.getLocalBounds();
                g_exitConfirmTextQuestion.setOrigin(bq.width / 2.f, bq.height / 2.f);
                g_exitConfirmTextQuestion.setPosition(1920.f / 2.f, 1080.f / 2.f - 150.f);
                g_exitConfirmTextInstruction.setString(L"Выход - нажмите ESC на клавиатуре,  остаться - нажмите Продолжить");
                auto bi = g_exitConfirmTextInstruction.getLocalBounds();
                g_exitConfirmTextInstruction.setOrigin(bi.width / 2.f, bi.height / 2.f);
                g_exitConfirmTextInstruction.setPosition(1920.f / 2.f, 1080.f / 2.f - 80.f);

                // показываем кнопку "Продолжить"
                g_actionButtonState = "CONTINUE";
                g_sprAction.setTexture(g_texContinue);
                g_sprAction.setColor(sf::Color(255, 255, 255, 255));
            }
            else
            {
                // первое нажатие ESC — показываем подтверждение
                g_uxMode = UxMode::ConfirmExit;
                g_exitConfirmActive = true;
                g_exitConfirmAlpha = 255.f;
                g_exitBgAlpha = 255.f; // ← сброс альфа фона
                g_exitConfirmFading = false;
                g_exitConfirmed = false;
                g_exitConfirmDelayTimer = 0.f;

                // показываем кнопку "Продолжить"
                g_actionButtonState = "CONTINUE";
                g_sprAction.setTexture(g_texContinue);
                g_sprAction.setColor(sf::Color(255, 255, 255, 255));
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
                for (size_t i = 0; i < g_vis_plr.size(); ++i)
                {
                    if (g_vis_plr[i].sprite.getGlobalBounds().contains(mp))
                    {
                        std::string s = ux_card_to_string(g_vis_plr[i].card);
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
                            g_vis_plr[i].shaking = true;
                            g_vis_plr[i].shakeTimer = 0.f;
                        }

                        break;
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
                }
            }
            // клик на сцене подтверждения выхода
            else if (g_uxMode == UxMode::ConfirmExit)
            {
                if (g_sprAction.getColor().a > 0 &&
                    g_sprAction.getGlobalBounds().contains(mp))
                {
                    // кнопка "Продолжить" — возвращаемся к игре
                    std::cout << "[UX] CONTINUE clicked - stay in game\n";

                    // скрываем сцену подтверждения
                    g_exitConfirmActive = false;
                    g_exitConfirmAlpha = 255.f;
                    g_exitBgAlpha = 255.f; // ← сброс альфа фона
                    g_exitConfirmFading = false;
                    g_exitConfirmed = false;
                    g_exitConfirmDelayTimer = 0.f;

                    // восстанавливаем тексты
                    g_exitConfirmTextQuestion.setString(L"Покинуть игру?");
                    auto bq = g_exitConfirmTextQuestion.getLocalBounds();
                    g_exitConfirmTextQuestion.setOrigin(bq.width / 2.f, bq.height / 2.f);
                    g_exitConfirmTextQuestion.setPosition(1920.f / 2.f, 1080.f / 2.f - 150.f);
                    g_exitConfirmTextInstruction.setString(L"Выход - нажмите ESC на клавиатуре,  остаться - нажмите Продолжить");
                    auto bi = g_exitConfirmTextInstruction.getLocalBounds();
                    g_exitConfirmTextInstruction.setOrigin(bi.width / 2.f, bi.height / 2.f);
                    g_exitConfirmTextInstruction.setPosition(1920.f / 2.f, 1080.f / 2.f - 80.f);

                    // скрываем кнопку
                    g_actionButtonFading = true;

                    // возвращаемся к предыдущему режиму
                    if (g_gameOverActive)
                        g_uxMode = UxMode::GameOver;
                    else
                        g_uxMode = UxMode::Idle;
                }
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
        // Воспроизводим звук взятия карты из колоды
        g_soundFromKol.play();

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

    else if (cmd.name == "TABLE_TO_HAND")
    {
        // Воспроизводим звук взятия карт со стола
        g_soundTake.play();

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
        // Воспроизводим звук колоды
        g_soundKoloda.play();
    }

    else if (cmd.name == "PLAY_ATTACK")
    {
        // Воспроизводим звук хода картой на стол
        g_soundFallDown.play();

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
        // Воспроизводим звук хода картой на стол
        g_soundFallDown.play();

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
        // Воспроизводим звук взятия карты из колоды
        g_soundFromKol.play();

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
        // Воспроизводим звук взятия карты из колоды
        g_soundFromKol.play();

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

        // Показываем значок козыря на месте пустой колоды
        if (g_showTrumpSuit)
        {
            g_trumpSuitText.setString(suit_to_symbol(c.suit));
            g_trumpSuitText.setFillColor(suit_color(c.suit));

            // центрируем относительно позиции козыря
            auto b = g_trumpSuitText.getLocalBounds();
            g_trumpSuitText.setOrigin(b.width / 2.f, b.height / 2.f);
            g_trumpSuitText.setPosition(g_layout.trump_pos);
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

        // скрыть значок козыря (масть)
        g_trumpSuitText.setFillColor(sf::Color(255, 255, 255, 0));
        g_showTrumpSuit = false;

        // кнопка CONTINUE
        g_actionButtonState = "CONTINUE";
        g_sprAction.setTexture(g_texContinue);
        g_sprAction.setColor(sf::Color(255, 255, 255, 255));
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
// DRAW TEXT WITH OUTLINE (для значка козыря)
// ------------------------------------------------------------
static void draw_text_with_outline(sf::RenderWindow &window, sf::Text &text, float outlineThickness = 2.f)
{
    sf::Color originalColor = text.getFillColor();
    sf::Vector2f originalPos = text.getPosition();

    // 8 направлений для обводки
    const float offsets[] = {
        -outlineThickness, -outlineThickness,
        0.f, -outlineThickness,
        outlineThickness, -outlineThickness,
        -outlineThickness, 0.f,
        outlineThickness, 0.f,
        -outlineThickness, outlineThickness,
        0.f, outlineThickness,
        outlineThickness, outlineThickness};

    // рисуем обводку (оранжевая)
    text.setFillColor(sf::Color(255, 165, 0, 255));
    for (int i = 0; i < 8; i++)
    {
        text.setPosition(
            originalPos.x + offsets[i * 2],
            originalPos.y + offsets[i * 2 + 1]);
        window.draw(text);
    }

    // рисуем основной текст поверх обводки
    text.setFillColor(originalColor);
    text.setPosition(originalPos);
    window.draw(text);
}

// ------------------------------------------------------------
// DRAW FRAME
// ------------------------------------------------------------
static void ux_draw_frame()
{
    g_window->clear();

    // === ОТДЕЛЬНАЯ СЦЕНА ПОДТВЕРЖДЕНИЯ ВЫХОДА ===
    if (g_exitConfirmActive)
    {
        // Рисуем ТОЛЬКО фон и элементы сцены выхода
        g_sprBg.setColor(sf::Color(255, 255, 255, (sf::Uint8)g_exitBgAlpha));
        g_window->draw(g_sprBg);

        g_window->draw(g_exitConfirmTextQuestion);
        if (!g_exitConfirmed || g_exitConfirmDelayTimer > 0.f)
            g_window->draw(g_exitConfirmTextInstruction);

        // кнопка рисуется после
        if (g_sprAction.getColor().a > 0)
        {
            auto c = g_sprAction.getColor();
            g_sprAction.setColor(sf::Color(c.r, c.g, c.b, (sf::Uint8)g_exitBgAlpha));
            g_window->draw(g_sprAction);
            g_sprAction.setColor(sf::Color(c.r, c.g, c.b, 255)); // восстанавливаем
        }

        g_window->display();
        return; // выходим — не рисуем игру
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
        if (g_trumpSuitOutline)
            draw_text_with_outline(*g_window, g_trumpSuitText, 2.f);
        else
            g_window->draw(g_trumpSuitText);
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

    // подсказка игроку (если видима)
    if (g_hintVisible || g_hintFading)
        g_window->draw(g_hintText);

    // finish him
    if (g_gameOverActive)
        g_window->draw(g_gameOverText);

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

        if (g_hintAlpha == 0.f)
        {
            g_hintFading = false;
            g_hintVisible = false;
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

    if (g_exitConfirmFading)
    {
        g_exitConfirmAlpha -= 255.f * 0.5f * dt; // 2 секунды на затухание (255 / 2 = 127.5 в сек)
        g_exitBgAlpha -= 255.f * 0.5f * dt;      // фон и кнопка затухают с той же скоростью

        if (g_exitConfirmAlpha < 0.f)
            g_exitConfirmAlpha = 0.f;
        if (g_exitBgAlpha < 0.f)
            g_exitBgAlpha = 0.f;

        g_exitConfirmTextQuestion.setFillColor(sf::Color(255, 165, 0, (sf::Uint8)g_exitConfirmAlpha));
        g_exitConfirmTextInstruction.setFillColor(sf::Color(255, 165, 0, (sf::Uint8)g_exitConfirmAlpha));

        if (g_exitConfirmAlpha == 0.f && g_exitBgAlpha == 0.f)
        {
            g_exitConfirmFading = false;
            g_exitConfirmActive = false;
            g_shouldExit = true; // теперь выходим
        }
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
            sort_hand(g_vis_plr, g_layout.center_x, g_layout.plr_y, g_handAnchorsPlrY, SortMode::ByRank);
            g_pendingSortPlr = false;
        }
        if (g_pendingSortBot)
        {
            sort_hand(g_vis_bot, g_layout.center_x, g_layout.bot_y, g_handAnchorsBotY, SortMode::ByRank);
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

    // сброс значка козыря
    g_trumpSuitText.setFillColor(sf::Color(255, 255, 255, 0));
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
    // Если когда‑нибудь появятся ресурсы, требующие освобождения —
    // это место для них.
    g_window = nullptr;
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
    ux_process_frame();
}
