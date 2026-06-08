#include <SFML/Graphics.hpp>
#include <iostream>
#include <fstream>
#include <vector>
#include <random>
using namespace std;
#include "ux_api.h"

struct PendingMoves
{
    std::vector<std::string> moves; // строки ходов
    bool active = false;
};
enum class UxMode
{
    Idle,
    WaitPlayerMove
};

PendingMoves g_pending;
UxMode g_uxMode = UxMode::Idle;

int g_chosenMove = -1;
bool g_moveReady = false;

void ux_start_wait_player_move(const std::vector<std::string> &moves)
{
    ux_start_wait_player_move_internal(moves); // твоя функция
    g_moveReady = false;
    g_chosenMove = -1;
}

void ux_process_frame()
{
    // это твой главный цикл одного кадра:
    // pollEvent, hover, shake, animate, draw
    process_ux_frame_internal();
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
    float center_y = 540; // добавлено: вертикальный центр стола

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
};

struct Slot
{
    sf::Vector2f pos;
    bool occupied = false;
    int pairIndex = -1; // индекс PairSlot в State.table
    bool isAttack = true;
    bool faceUp = true;
};

vector<Slot> attackSlots;        // size = max_pairs (6)
vector<Slot> defendSlots;        // size = max_pairs (6)
vector<CardVisual> tableVisuals; // визуалки, которые уже на столе (InSlot)

std::string actionButtonState = "NONE";

// anchors для рук, заполняются из JSON (мы используем только Y)
vector<float> handAnchorsPlrY;
vector<float> handAnchorsBotY;
// ------------------------------------------------------------
// LOAD LAYOUT
// ------------------------------------------------------------
Layout load_layout()
{
    Layout L;
    L.discard_exit = {-200.f, L.center_y};

    ifstream f("layout_game.json");
    if (!f.is_open())
    {
        cout << "NO JSON\n";
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

    // center X = игрока
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
    // ЗАГРУЗКА МАССИВОВ СЛОТОВ И РУК ИЗ JSON
    // ----------------------------
    // Универсальный парсер массива пар { "x":..., "y":... }
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
    // Парсер для рук: читаем массив { "x":..., "y":... } но сохраняем только Y
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
            // float x = std::stof(arr.substr(xColon + 1, xEnd - (xColon + 1)));

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

    handAnchorsPlrY.clear();
    handAnchorsBotY.clear();
    parseHandYArray("\"player_hand\"", handAnchorsPlrY);
    parseHandYArray("\"bot_hand\"", handAnchorsBotY);
    // Заполняем attackSlots и defendSlots глобально
    attackSlots.clear();
    defendSlots.clear();
    parseXYArray("\"attack_slots\"", attackSlots, true);
    parseXYArray("\"defense_slots\"", defendSlots, false);

    // Если center_y не задан явно, вычислим как среднее Y слотов (если есть)
    if (!attackSlots.empty())
    {
        float sumY = 0.f;
        for (auto &s : attackSlots)
            sumY += s.pos.y;
        L.center_y = sumY / attackSlots.size();
    }
    else if (!defendSlots.empty())
    {
        float sumY = 0.f;
        for (auto &s : defendSlots)
            sumY += s.pos.y;
        L.center_y = sumY / defendSlots.size();
    }

    return L;
}

// ------------------------------------------------------------
// FULL DECK
// ------------------------------------------------------------
vector<Card> make_full_deck()
{
    vector<Card> d;
    for (int s = 0; s < 4; s++)
        for (int r = 6; r <= 14; r++)
            d.push_back({r, s});
    return d;
}

int find_first_free_slot(vector<Slot> &slots)
{
    for (int i = 0; i < (int)slots.size(); ++i)
        if (!slots[i].occupied)
            return i;
    return -1;
}
// ------------------------------------------------------------
// HAND LAYOUT (W = 1240 px)
// ------------------------------------------------------------
// layout_hand: если anchorsY не пустой — используем только Y из anchors (вертикаль).
// По X всегда используем алгоритм: режим до 10 карт (step = cardWidth + gap) или сжатие в W.
// layout_hand: X вычисляем алгоритмически (центр + step), Y берём из anchorsY если они есть,
// иначе используем переданный y.
// Функция только задаёт targetPos, НЕ трогает sprite.setPosition().
// ------------------------------------------------------------
// HAND LAYOUT (W = 1240 px)
// ------------------------------------------------------------
void layout_hand(vector<CardVisual> &hand, float centerX, float y, const vector<float> &anchorsY)
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
        step = cardWidth + gap; // 134 px
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
float easeOutQuad(float t) { return 1.f - (1.f - t) * (1.f - t); }

// ------------------------------------------------------------
// ANIMATION
// ------------------------------------------------------------
void animate_cards(vector<CardVisual> &hand,
                   sf::Texture cardTex[4][9],
                   const sf::Texture &texBack,
                   const Layout &L)
{
    const float speed = 12.f;

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

            // mid-flight flip для бота
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
                tableVisuals.push_back(v);
                it = hand.erase(it);

                if (v.owner == PLR)
                    layout_hand(hand, L.center_x, L.plr_y, handAnchorsPlrY);
                else
                    layout_hand(hand, L.center_x, L.bot_y, handAnchorsBotY);
            }
            else
            {
                v.state = InHand;
                ++it;
            }
        }
    }
}

std::string ux_card_to_string(const Card &c)
{
    // rank
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

    // suit
    char s = "cdhs"[c.suit];

    return r + s;
}

//----------
// choose card
//-------------------------------------------------------------------
void update_hover_effects(std::vector<CardVisual> &hand,
                          const std::vector<std::string> &validMoves,
                          sf::Vector2f mousePos)
{
    for (auto &cv : hand)
    {
        if (cv.animating)
            continue;

        // строка карты
        std::string s = ux_card_to_string(cv.card);

        // валидна ли карта
        bool isValid = false;
        for (auto &m : validMoves)
            if (m == s)
                isValid = true;

        // находится ли мышь над картой
        bool isHover = cv.sprite.getGlobalBounds().contains(mousePos);

        if (isHover && isValid)
        {
            cv.hovered = true;
            cv.liftOffset = -10.f; // поднять
        }
        else
        {
            cv.hovered = false;
            cv.liftOffset = 0.f; // вернуть назад
        }
    }
}
void update_card_effects(std::vector<CardVisual> &hand, float dt)
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
void add_card_to_hand(vector<CardVisual> &hand,
                      const Card &c,
                      sf::Texture cardTex[4][9],
                      const sf::Texture &texBack,
                      const sf::Vector2f &deckPos,
                      float centerX,
                      float y,
                      bool faceUp,
                      Side owner)
{
    CardVisual v;
    v.card = c;
    if (faceUp)
        v.sprite.setTexture(cardTex[c.suit][c.rank - 6]);
    else
        v.sprite.setTexture(texBack);

    v.sprite.setOrigin(62, 90);

    v.currentPos = deckPos;
    v.targetPos = deckPos;
    v.sprite.setPosition(deckPos);

    v.animating = true;
    v.state = InHand;
    v.owner = owner;
    v.flipMidFlight = false;
    v.initialDist = 0.f;

    hand.push_back(v);

    // пересчитать цели для всех карт руки
    if (owner == PLR)
        layout_hand(hand, centerX, y, handAnchorsPlrY);
    else
        layout_hand(hand, centerX, y, handAnchorsBotY);
}
// handIndex — индекс в векторе руки (0 = первая карта в руке)
// slots — ссылка на attackSlots или defendSlots
// slotIndex — индекс слота в этом векторе
// pairIndex — индекс PairSlot в State.table (если нужен) — можно передавать slotIndex
// faceUpForBot — если false, показываем рубашку; если true — лицом
// ------------------------------------------------------------
// PLAY CARD TO SLOT (HAND -> SLOT)
// ------------------------------------------------------------
void play_card_to_slot(vector<CardVisual> &hand,
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

std::string rank_to_str(int r)
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

void start_discard_animation(const Layout &L)
{
    // 1. Все карты на столе отправляем в отбой
    for (auto &v : tableVisuals)
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
    for (auto &s : attackSlots)
    {
        s.occupied = false;
        s.pairIndex = -1;
    }
    for (auto &s : defendSlots)
    {
        s.occupied = false;
        s.pairIndex = -1;
    }
}
void start_table_to_hand(Side taker,
                         vector<CardVisual> &vis_plr,
                         vector<CardVisual> &vis_bot,
                         sf::Texture cardTex[4][9],
                         const sf::Texture &texBack,
                         const Layout &L)
{
    vector<CardVisual> &hand = (taker == PLR ? vis_plr : vis_bot);

    for (auto &v : tableVisuals)
    {
        CardVisual nv;
        nv.card = v.card;

        // текстура: PLR — лицом, BOT — рубашкой
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
    tableVisuals.clear();

    for (auto &s : attackSlots)
    {
        s.occupied = false;
        s.pairIndex = -1;
    }
    for (auto &s : defendSlots)
    {
        s.occupied = false;
        s.pairIndex = -1;
    }

    // пересчитать раскладку руки
    if (taker == PLR)
        layout_hand(vis_plr, L.center_x, L.plr_y, handAnchorsPlrY);
    else
        layout_hand(vis_bot, L.center_x, L.bot_y, handAnchorsBotY);
}
Card parse_card(const std::string &s)
{
    // формат: "6C", "10D", "QH", "AS"
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

bool anyAnimating(const vector<CardVisual> &a,
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

struct UxCommand
{
    std::string name;
    std::vector<std::string> args;
};

//-----
void ux_start_wait_player_move(const std::vector<std::string> &moves)
{
    g_pending.moves = moves;
    g_pending.active = true;
    g_uxMode = UxMode::WaitPlayerMove;

    std::cout << "[UX] Waiting for player move, moves = ";
    for (auto &m : moves)
        std::cout << m << " ";
    std::cout << "\n";
}

int ux_find_move_index_for_string(const std::string &s)
{
    if (!g_pending.active)
        return -1;

    for (int i = 0; i < (int)g_pending.moves.size(); ++i)
        if (g_pending.moves[i] == s)
            return i;

    return -1;
}

int ux_find_pass_index()
{
    return ux_find_move_index_for_string("p");
}

// ------------------------------------------------------------
// MAIN
// ------------------------------------------------------------
int main()
{
    sf::RenderWindow win(sf::VideoMode(1920, 1080), "UX Sandbox", sf::Style::Fullscreen);
    win.setVerticalSyncEnabled(true);
    win.setKeyRepeatEnabled(false);

    sf::Clock clock;
    float dt = 0.f;

    // ---------------------------
    // LOAD BACKGROUND
    // ---------------------------
    sf::Texture texBg;
    texBg.loadFromFile("F:/MYPROG/DURAK2/cards/background.png");
    sf::Sprite sprBg(texBg);

    // ---------------------------
    // LOAD CARD TEXTURES
    // ---------------------------
    sf::Texture texBack;
    texBack.loadFromFile("F:/MYPROG/DURAK2/cards/back.png");

    sf::Texture cardTex[4][9];
    // load buttons...
    sf::Texture texActionTake;
    sf::Texture texActionBeat;
    sf::Texture texActionGive;

    texActionTake.loadFromFile("F:/MYPROG/DURAK2/buttons/action_take.png");
    texActionBeat.loadFromFile("F:/MYPROG/DURAK2/buttons/action_beat.png");
    texActionGive.loadFromFile("F:/MYPROG/DURAK2/buttons/action_give.png");

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

    for (int s = 0; s < 4; s++)
    {
        for (int r = 6; r <= 14; r++)
        {
            std::string path = "F:/MYPROG/DURAK2/cards/";
            path += rank_to_str(r);
            path += suitChar(s);
            path += ".png";

            if (!cardTex[s][r - 6].loadFromFile(path))
                std::cout << "ERROR loading: " << path << "\n";
        }
    }

    // ---------------------------
    // LOAD LAYOUT
    // ---------------------------
    Layout L = load_layout();

    // ---------------------------
    // VISUAL STATE
    // ---------------------------
    vector<CardVisual> vis_plr, vis_bot;

    sf::Sprite trumpSpr, deckSpr;
    trumpSpr.setTexture(cardTex[0][0]); // временно
    trumpSpr.setOrigin(62, 90);
    trumpSpr.setPosition(L.trump_pos);
    trumpSpr.setRotation(L.trump_angle);

    deckSpr.setTexture(texBack);
    deckSpr.setOrigin(62, 90);
    deckSpr.setPosition(L.deck_pos);
    deckSpr.setRotation(L.deck_angle);

    sf::Sprite sprAction;
    if (L.has_action)
    {
        // временная текстура до первой команды SET_ACTION_BUTTON
        sprAction.setTexture(texActionTake);
        sprAction.setOrigin(texActionTake.getSize().x / 2, texActionTake.getSize().y / 2);
        sprAction.setPosition(L.action_pos);
        sprAction.setColor(sf::Color(255, 255, 255, 0)); // скрыта до первой команды
    }

    bool trumpVisible = true;
    bool deckVisible = true;
    int deckSize = 24;

    // ---------------------------
    // UX SCRIPT + FLAGS
    // ---------------------------
    std::vector<UxCommand> script;
    int currentCmd = 0;
    bool animatingNow = false;
    bool dealingBurst = false;
    bool waitActive = false;

    float dealTimer = 0.f;
    float dealInterval = 0.15f;

    // ---------------------------
    // run_command
    // ---------------------------
    auto run_command = [&](const UxCommand &cmd)
    {
        if (cmd.name == "DEAL_CARD")
        {
            animatingNow = true;

            Side s = (cmd.args[0] == "PLR" ? PLR : BOT);
            Card c = parse_card(cmd.args[1]);
            bool faceUp = (s == PLR);

            if (s == PLR)
                add_card_to_hand(vis_plr, c, cardTex, texBack, L.deck_pos, L.center_x, L.plr_y, faceUp, PLR);
            else
                add_card_to_hand(vis_bot, c, cardTex, texBack, L.deck_pos, L.center_x, L.bot_y, faceUp, BOT);
        }
        else if (cmd.name == "SET_TRUMP")
        {
            Card c = parse_card(cmd.args[0]);
            trumpSpr.setTexture(cardTex[c.suit][c.rank - 6]);
            trumpVisible = true;
        }
        else if (cmd.name == "UPDATE_DECK_SIZE")
        {
            int n = std::stoi(cmd.args[0]);
            deckSize = n;
            deckVisible = (n > 1); //  рисуем когда
        }
        else if (cmd.name == "STARTING_DEAL")
        {
            dealingBurst = true;
            dealTimer = 0.f;
        }
        else if (cmd.name == "TABLE_TO_HAND")
        {
            animatingNow = true;

            Side taker = (cmd.args[0] == "PLR" ? PLR : BOT);

            start_table_to_hand(
                taker,
                vis_plr,
                vis_bot,
                cardTex,
                texBack,
                L);
        }
        else if (cmd.name == "PLAY_ATTACK")
        {
            animatingNow = true;

            Side s = (cmd.args[0] == "PLR" ? PLR : BOT);
            Card c = parse_card(cmd.args[1]);
            int slotIndex = std::stoi(cmd.args[2]);

            vector<CardVisual> &hand = (s == PLR ? vis_plr : vis_bot);
            int handIndex = -1;
            for (int i = 0; i < (int)hand.size(); i++)
                if (hand[i].card.rank == c.rank && hand[i].card.suit == c.suit)
                    handIndex = i;

            if (handIndex != -1)
            {
                play_card_to_slot(
                    hand,
                    handIndex,
                    attackSlots,
                    slotIndex,
                    slotIndex,
                    true,
                    cardTex,
                    texBack);
            }
        }
        else if (cmd.name == "PLAY_DEFENSE")
        {
            animatingNow = true;

            Side s = (cmd.args[0] == "PLR" ? PLR : BOT);
            Card c = parse_card(cmd.args[1]);
            int slotIndex = std::stoi(cmd.args[2]);

            vector<CardVisual> &hand = (s == PLR ? vis_plr : vis_bot);
            int handIndex = -1;
            for (int i = 0; i < (int)hand.size(); i++)
                if (hand[i].card.rank == c.rank && hand[i].card.suit == c.suit)
                    handIndex = i;

            if (handIndex != -1)
            {
                play_card_to_slot(
                    hand,
                    handIndex,
                    defendSlots,
                    slotIndex,
                    slotIndex,
                    true,
                    cardTex,
                    texBack);
            }
        }
        else if (cmd.name == "CLEAR_TABLE")
        {
            animatingNow = true;
            start_discard_animation(L);
        }
        else if (cmd.name == "WAIT")
        {
            waitActive = true;
        }
        else if (cmd.name == "DEAL_PREVOISE_TRUMP")
        {
            animatingNow = true;

            Side s = (cmd.args[0] == "PLR" ? PLR : BOT);
            Card c = parse_card(cmd.args[1]);
            bool faceUp = (s == PLR);

            // анимация из позиции колоды (рубашка)
            if (s == PLR)
                add_card_to_hand(vis_plr, c, cardTex, texBack, L.deck_pos, L.center_x, L.plr_y, faceUp, PLR);
            else
                add_card_to_hand(vis_bot, c, cardTex, texBack, L.deck_pos, L.center_x, L.bot_y, faceUp, BOT);

            // ВАЖНО: как только пошла предпоследняя карта — рубашку больше не рисуем
            deckVisible = false;
        }

        else if (cmd.name == "DEAL_LAST_TRUMP")
        {
            animatingNow = true;

            Side s = (cmd.args[0] == "PLR" ? PLR : BOT);
            Card c = parse_card(cmd.args[1]);
            bool faceUp = (s == PLR);

            // анимация из позиции козыря
            if (s == PLR)
                add_card_to_hand(vis_plr, c, cardTex, texBack, L.trump_pos, L.center_x, L.plr_y, faceUp, PLR);
            else
                add_card_to_hand(vis_bot, c, cardTex, texBack, L.trump_pos, L.center_x, L.bot_y, faceUp, BOT);

            // скрываем козырь
            trumpVisible = false;
        }
        else if (cmd.name == "SET_ACTION_BUTTON")
        {
            actionButtonState = cmd.args[0];

            if (actionButtonState == "NONE")
            {
                // скрыть кнопку
                sprAction.setColor(sf::Color(255, 255, 255, 0));
            }
            else
            {
                // показать кнопку
                sprAction.setColor(sf::Color(255, 255, 255, 255));

                // выбрать текстуру кнопки
                if (actionButtonState == "TAKE")
                    sprAction.setTexture(texActionTake);
                else if (actionButtonState == "BEAT")
                    sprAction.setTexture(texActionBeat);
                else if (actionButtonState == "GIVE")
                    sprAction.setTexture(texActionGive);
            }
        }
    };

    // ---------------------------
    // SCRIPT: раздача + одна волна атаки
    // ---------------------------
    script = {

        {"STARTING_DEAL", {}},

        {"DEAL_CARD", {"PLR", "AD"}},
        {"DEAL_CARD", {"PLR", "QS"}},
        {"DEAL_CARD", {"PLR", "QD"}},
        {"DEAL_CARD", {"PLR", "6D"}},
        {"DEAL_CARD", {"PLR", "9S"}},
        {"DEAL_CARD", {"PLR", "10S"}},

        {"DEAL_CARD", {"BOT", "KH"}},
        {"DEAL_CARD", {"BOT", "JH"}},
        {"DEAL_CARD", {"BOT", "7D"}},
        {"DEAL_CARD", {"BOT", "KS"}},
        {"DEAL_CARD", {"BOT", "9D"}},
        {"DEAL_CARD", {"BOT", "8C"}},

        {"SET_TRUMP", {"9C"}},
        {"UPDATE_DECK_SIZE", {"24"}},
        {"SET_ACTION_BUTTON", {"NONE"}},
        {"WAIT", {}},

        // --- атака 1 ---
        {"PLAY_ATTACK", {"BOT", "7D", "0"}},
        {"SET_ACTION_BUTTON", {"TAKE"}},
        {"WAIT", {}},

        {"PLAY_DEFENSE", {"PLR", "AD", "0"}},
        {"SET_ACTION_BUTTON", {"NONE"}},
        {"WAIT", {}},

        {"SET_ACTION_BUTTON", {"NONE"}},
        {"WAIT", {}},

        // --- конец волны ---
        {"CLEAR_TABLE", {"7D", "AD"}},
        {"UPDATE_DECK_SIZE", {"22"}},
        {"DEAL_CARD", {"BOT", "6C"}},
        {"DEAL_CARD", {"PLR", "10D"}},
        {"SET_ACTION_BUTTON", {"NONE"}},
        {"WAIT", {}},

        // --- атака 2 ---
        {"PLAY_ATTACK", {"PLR", "10S", "0"}},
        {"SET_ACTION_BUTTON", {"NONE"}},
        {"WAIT", {}},

        {"PLAY_DEFENSE", {"BOT", "KS", "0"}},
        {"SET_ACTION_BUTTON", {"BEAT"}},
        {"WAIT", {}},

        {"PLAY_ATTACK", {"PLR", "10D", "1"}},
        {"SET_ACTION_BUTTON", {"NONE"}},
        {"WAIT", {}},

        {"PLAY_DEFENSE", {"BOT", "6C", "1"}},
        {"SET_ACTION_BUTTON", {"BEAT"}},
        {"WAIT", {}},

        {"PLAY_ATTACK", {"PLR", "6D", "2"}},
        {"SET_ACTION_BUTTON", {"NONE"}},
        {"WAIT", {}},

        {"PLAY_DEFENSE", {"BOT", "9D", "2"}},
        {"SET_ACTION_BUTTON", {"BEAT"}},
        {"WAIT", {}},

        {"PLAY_ATTACK", {"PLR", "9S", "3"}},
        {"SET_ACTION_BUTTON", {"NONE"}},
        {"WAIT", {}},

        {"PLAY_DEFENSE", {"BOT", "8C", "3"}},
        {"SET_ACTION_BUTTON", {"BEAT"}},
        {"WAIT", {}},

        {"SET_ACTION_BUTTON", {"NONE"}},
        {"WAIT", {}},

        // --- конец волны ---
        {"CLEAR_TABLE", {"10S", "KS", "10D", "6C", "6D", "9D", "9S", "8C"}},
        {"UPDATE_DECK_SIZE", {"14"}},
        {"DEAL_CARD", {"PLR", "10C"}},
        {"DEAL_CARD", {"PLR", "JD"}},
        {"DEAL_CARD", {"PLR", "KD"}},
        {"DEAL_CARD", {"PLR", "6H"}},
        {"DEAL_CARD", {"BOT", "7C"}},
        {"DEAL_CARD", {"BOT", "9H"}},
        {"DEAL_CARD", {"BOT", "8S"}},
        {"DEAL_CARD", {"BOT", "JS"}},
        {"SET_ACTION_BUTTON", {"NONE"}},
        {"WAIT", {}},

        // --- атака 3 ---
        {"PLAY_ATTACK", {"BOT", "8S", "0"}},
        {"SET_ACTION_BUTTON", {"TAKE"}},
        {"WAIT", {}},

        {"PLAY_DEFENSE", {"PLR", "QS", "0"}},
        {"SET_ACTION_BUTTON", {"NONE"}},
        {"WAIT", {}},

        {"SET_ACTION_BUTTON", {"NONE"}},
        {"WAIT", {}},

        // --- конец волны ---
        {"CLEAR_TABLE", {"8S", "QS"}},
        {"UPDATE_DECK_SIZE", {"12"}},
        {"DEAL_CARD", {"BOT", "8D"}},
        {"DEAL_CARD", {"PLR", "10H"}},
        {"SET_ACTION_BUTTON", {"NONE"}},
        {"WAIT", {}},

        // --- атака 4 ---
        {"PLAY_ATTACK", {"PLR", "10C", "0"}},
        {"SET_ACTION_BUTTON", {"NONE"}},
        {"WAIT", {}},

        {"SET_ACTION_BUTTON", {"GIVE"}},
        {"WAIT", {}},

        {"PLAY_ATTACK", {"PLR", "10H", "1"}},
        {"SET_ACTION_BUTTON", {"GIVE"}},
        {"WAIT", {}},

        {"TABLE_TO_HAND", {"BOT"}},
        {"UPDATE_DECK_SIZE", {"10"}},
        {"DEAL_CARD", {"PLR", "8H"}},
        {"DEAL_CARD", {"PLR", "AS"}},
        {"SET_ACTION_BUTTON", {"NONE"}},
        {"WAIT", {}},

        // --- атака 5 ---
        {"PLAY_ATTACK", {"PLR", "8H", "0"}},
        {"SET_ACTION_BUTTON", {"NONE"}},
        {"WAIT", {}},

        {"PLAY_DEFENSE", {"BOT", "9H", "0"}},
        {"SET_ACTION_BUTTON", {"BEAT"}},
        {"WAIT", {}},

        {"SET_ACTION_BUTTON", {"NONE"}},
        {"WAIT", {}},

        // --- конец волны ---
        {"CLEAR_TABLE", {"8H", "9H"}},
        {"UPDATE_DECK_SIZE", {"9"}},
        {"DEAL_CARD", {"PLR", "6S"}},
        {"SET_ACTION_BUTTON", {"NONE"}},
        {"WAIT", {}},

        // --- атака 6 ---
        {"PLAY_ATTACK", {"BOT", "8D", "0"}},
        {"SET_ACTION_BUTTON", {"TAKE"}},
        {"WAIT", {}},

        {"PLAY_DEFENSE", {"PLR", "QD", "0"}},
        {"SET_ACTION_BUTTON", {"NONE"}},
        {"WAIT", {}},

        {"SET_ACTION_BUTTON", {"NONE"}},
        {"WAIT", {}},

        // --- конец волны ---
        {"CLEAR_TABLE", {"8D", "QD"}},
        {"UPDATE_DECK_SIZE", {"8"}},
        {"DEAL_CARD", {"PLR", "JC"}},
        {"SET_ACTION_BUTTON", {"NONE"}},
        {"WAIT", {}},

        // --- атака 7 ---
        {"PLAY_ATTACK", {"PLR", "6H", "0"}},
        {"SET_ACTION_BUTTON", {"NONE"}},
        {"WAIT", {}},

        {"PLAY_DEFENSE", {"BOT", "10H", "0"}},
        {"SET_ACTION_BUTTON", {"BEAT"}},
        {"WAIT", {}},

        {"PLAY_ATTACK", {"PLR", "6S", "1"}},
        {"SET_ACTION_BUTTON", {"NONE"}},
        {"WAIT", {}},

        {"PLAY_DEFENSE", {"BOT", "10C", "1"}},
        {"SET_ACTION_BUTTON", {"BEAT"}},
        {"WAIT", {}},

        {"SET_ACTION_BUTTON", {"NONE"}},
        {"WAIT", {}},

        // --- конец волны ---
        {"CLEAR_TABLE", {"6H", "10H", "6S", "10C"}},
        {"UPDATE_DECK_SIZE", {"4"}},
        {"DEAL_CARD", {"PLR", "AC"}},
        {"DEAL_CARD", {"PLR", "7S"}},
        {"DEAL_CARD", {"BOT", "KC"}},
        {"DEAL_CARD", {"BOT", "QH"}},
        {"SET_ACTION_BUTTON", {"NONE"}},
        {"WAIT", {}},

        // --- атака 8 ---
        {"PLAY_ATTACK", {"BOT", "JH", "0"}},
        {"SET_ACTION_BUTTON", {"TAKE"}},
        {"WAIT", {}}};

    // ---------------------------
    // MAIN LOOP
    // ---------------------------
    while (win.isOpen())
    {
        dt = clock.restart().asSeconds();

        sf::Event e;
        while (win.pollEvent(e))
        {
            sf::Vector2i mpix = sf::Mouse::getPosition(win);
            sf::Vector2f mp = win.mapPixelToCoords(mpix);

            if (g_uxMode == UxMode::WaitPlayerMove && g_pending.active)
            {
                update_hover_effects(vis_plr, g_pending.moves, mp);
            }

            if (e.type == sf::Event::Closed)
                win.close();
            if (e.type == sf::Event::KeyPressed && e.key.code == sf::Keyboard::Escape)
                win.close();

            if (e.type == sf::Event::KeyPressed && e.key.code == sf::Keyboard::M)
            {
                std::vector<std::string> moves;

                // все карты игрока как допустимые ходы
                for (auto &cv : vis_plr)
                    moves.push_back(ux_card_to_string(cv.card));

                // и PASS
                moves.push_back("p");

                ux_start_wait_player_move(moves);
            }

            if (e.type == sf::Event::MouseButtonPressed &&
                e.mouseButton.button == sf::Mouse::Left)
            {
                sf::Vector2f mp(e.mouseButton.x, e.mouseButton.y);

                if (g_uxMode == UxMode::WaitPlayerMove && g_pending.active)
                {
                    for (size_t i = 0; i < vis_plr.size(); ++i)
                    {
                        if (vis_plr[i].sprite.getGlobalBounds().contains(mp))
                        {
                            std::string s = ux_card_to_string(vis_plr[i].card);
                            int idx = ux_find_move_index_for_string(s);

                            if (idx >= 0)
                            {
                                // допустимый ход

                                std::cout << "[UX] CHOSEN CARD " << s
                                          << " (move idx = " << idx << ")\n";

                                g_pending.active = false;
                                g_uxMode = UxMode::Idle;
                                g_chosenMove = idx;
                                g_moveReady = true;
                            }
                            else
                            {
                                std::cout << "[UX] INVALID CARD " << s << "\n";
                                vis_plr[i].shaking = true;
                                vis_plr[i].shakeTimer = 0.f;
                            }

                            break;
                        }
                    }

                    // PASS-кнопка
                    if (sprAction.getGlobalBounds().contains(mp))
                    {
                        int idx = ux_find_pass_index();
                        if (idx >= 0)
                        {
                            std::cout << "[UX] CHOSEN PASS (idx = " << idx << ")\n";
                            g_pending.active = false;
                            g_uxMode = UxMode::Idle;
                            g_chosenMove = idx;
                            g_moveReady = true;
                        }
                        else
                        {
                            std::cout << "[UX] PASS not allowed\n";
                        }
                    }
                }
            }
        }

        // ---------------------------
        // UX SCRIPT EXECUTION
        // ---------------------------

        // WAIT — ждём конца анимации, но НЕ блокируем animate_cards()
        if (waitActive)
        {
            if (!anyAnimating(vis_plr, vis_bot, tableVisuals))
            {
                waitActive = false;
                animatingNow = false;
            }
        }
        // burst-раздача
        else if (dealingBurst)
        {
            dealTimer += dt;
            if (dealTimer >= dealInterval)
            {
                dealTimer = 0.f;
                if (currentCmd < (int)script.size())
                    run_command(script[currentCmd++]);
                else
                    dealingBurst = false;
            }
        }
        // обычная команда
        else if (!animatingNow && currentCmd < (int)script.size())
        {
            run_command(script[currentCmd++]);
        }

        //----- выбор карты--------
        update_card_effects(vis_plr, dt);
        // ---------------------------
        // ANIMATION UPDATE
        // ---------------------------
        animate_cards(vis_plr, cardTex, texBack, L);
        animate_cards(vis_bot, cardTex, texBack, L);
        animate_cards(tableVisuals, cardTex, texBack, L);

        if (!anyAnimating(vis_plr, vis_bot, tableVisuals))
            animatingNow = false;

        // ---------------------------
        // DRAW
        // ---------------------------
        win.clear();
        win.draw(sprBg);

        // колода и козырь
        if (trumpVisible)
            win.draw(trumpSpr);
        if (deckVisible && deckSize > 1)
            win.draw(deckSpr);

        // карты на столе
        for (auto &v : tableVisuals)
            win.draw(v.sprite);

        // карты в руках (статичные)
        for (auto &v : vis_plr)
            if (!v.animating)
                win.draw(v.sprite);
        for (auto &v : vis_bot)
            if (!v.animating)
                win.draw(v.sprite);

        // карты в руках (анимирующиеся)
        for (auto &v : vis_plr)
            if (v.animating)
                win.draw(v.sprite);
        for (auto &v : vis_bot)
            if (v.animating)
                win.draw(v.sprite);

        // --- ACTION BUTTON (всегда поверх всего) ---
        if (sprAction.getColor().a > 0)
            win.draw(sprAction);

        win.display();
    }

    return 0;
}