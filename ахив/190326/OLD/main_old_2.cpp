#include <bits/stdc++.h>

using namespace std;

static std::mt19937 rng((unsigned)time(nullptr));

// F:\MYPROG\DURAK2>g++ main_old_2.cpp -o durak_txt
// -------------------------------------------------------
//  SET UTILITIES FOR KNOWLEDGE MODEL
// -------------------------------------------------------

//  -------------------------
//  ENUMS
//  -------------------------

enum Side
{
    PLR,
    BOT
};

enum Phase
{
    PH_ATTACK_START,
    PH_ATTACK_CONTINUE,
    PH_DEFENSE_CONTINUE,
    PH_END_OF_WAVE,
    PH_EXTRA_ATTACK,
    PH_GAMEOVER_PLR_WIN,
    PH_GAMEOVER_BOT_WIN,
    PH_GAMEOVER_DRAW,
    PH_INVALID_STATE
};
enum class UpdateContext
{
    PLAY_CARD,      // обычный ход картой
    PASS_NO_TAKE,   // пас, но никто ничего не берёт
    DEFENDER_TAKES, // защитник забирает стол
    WAVE_END        // волна завершена, стол ушёл в сброс
};
// -------------------------
// CARD STRUCTURES
// -------------------------

struct Card
{
    int rank; // 6..14
    int suit; // 0..3

    bool operator==(const Card &o) const
    {
        return rank == o.rank && suit == o.suit;
    }
};

struct PairSlot
{
    Card attack;
    bool has_defense = false;
    Card defense;
};

//----------------------- для множеств ----------------------
struct CardLess
{
    bool operator()(const Card &a, const Card &b) const
    {
        if (a.suit != b.suit)
            return a.suit < b.suit;
        return a.rank < b.rank;
    }
};

using CardSet = std::set<Card, CardLess>;

CardSet set_union(const CardSet &a, const CardSet &b)
{
    CardSet r = a;
    r.insert(b.begin(), b.end());
    return r;
}

CardSet set_diff(const CardSet &a, const CardSet &b)
{
    CardSet r;
    std::set_difference(
        a.begin(), a.end(),
        b.begin(), b.end(),
        std::inserter(r, r.begin()),
        CardLess());
    return r;
}

bool same_card(const Card &a, const Card &b)
{
    return a.rank == b.rank && a.suit == b.suit;
}
//---- move
struct Move
{
    bool is_pass;
    Card card;

    static Move Pass()
    {
        Move m;
        m.is_pass = true;
        return m;
    }
    static Move Play(Card c)
    {
        Move m;
        m.is_pass = false;
        m.card = c;
        return m;
    }
};
// -------------------------
// STATE
// -------------------------

struct State
{
    Side attacker;
    Side defender;
    Side actor;

    vector<Card> hand_plr;
    vector<Card> hand_bot;

    vector<Card> deck;
    vector<Card> discard;

    Card trump_card;

    vector<PairSlot> table;

    int max_pairs = 6;

    CardSet known_in_opponent_hand[2];       // Что мы точно знаем о руке оппонента
    CardSet assumed_opponent_hand[2];        // Что теоретически может быть у оппонента
    CardSet assumed_not_in_opponent_hand[2]; // Что точно НЕ в руке оппонента, но в игре
    CardSet possible_in_opponent_hand[2];    // Что МОЖЕТ быть у оппонента (финальная оценка)
};
// -------------------------------
//  HELPERS FOR KNOWLEDGE MODEL
// -------------------------------

// Удалить карту из множества (если есть)
void remove_from_set(CardSet &s, const Card &c)
{
    auto it = s.find(c);
    if (it != s.end())
        s.erase(it);
}

// Добавить ВСЕ карты со стола в known_in_opponent_hand[viewer]
void add_table_to_known(State &st, Side viewer)
{
    auto &k = st.known_in_opponent_hand[viewer];

    for (auto &p : st.table)
    {
        // карта атаки всегда есть
        k.insert(p.attack);

        // карта защиты есть только если has_defense == true
        if (p.has_defense)
            k.insert(p.defense);
    }
}

// Все карты, которые бьют карту c (или равны ей), включая саму карту
CardSet all_that_beat_or_equal(const Card &c, int trump_suit)
{
    CardSet res;

    for (int s = 0; s < 4; ++s)
    {
        for (int r = 6; r <= 14; ++r)
        {
            Card x{r, s};
            bool ok = false;

            if (s == c.suit && r >= c.rank)
                ok = true;

            if (s == trump_suit && c.suit != trump_suit)
                ok = true;

            if (same_card(x, c))
                ok = true;

            if (ok)
                res.insert(x);
        }
    }

    return res;
}

// Все карты, которые строго сильнее карты d
CardSet strictly_stronger(const Card &d, int trump_suit)
{
    CardSet res;

    for (int s = 0; s < 4; ++s)
    {
        for (int r = 6; r <= 14; ++r)
        {
            Card x{r, s};
            bool stronger = false;

            if (s == d.suit && r > d.rank)
                stronger = true;

            if (s == trump_suit && d.suit != trump_suit)
                stronger = true;

            if (stronger)
                res.insert(x);
        }
    }

    return res;
}

CardSet to_set(const std::vector<Card> &v)
{
    CardSet s;
    for (auto &c : v)
        s.insert(c);
    return s;
}

// -------------------------------
//  HAND ACCESS HELPERS
// -------------------------------

// Константный доступ к руке игрока
const vector<Card> &hand_of_const(const State &st, Side s)
{
    return (s == PLR ? st.hand_plr : st.hand_bot);
}

// Неконстантный доступ к руке игрока
vector<Card> &hand_of(State &st, Side s)
{
    return (s == PLR ? st.hand_plr : st.hand_bot);
}

double card_strength(const Card &c, int trump_suit)
{
    static std::map<int, std::pair<double, double>> values = {
        {6, {1, 6}}, {7, {1.5, 6.5}}, {8, {2, 7}}, {9, {2.5, 7.5}}, {10, {3, 8}}, {11, {3.5, 8.5}}, {12, {4, 9}}, {13, {4.5, 9.5}}, {14, {5, 10}}};

    auto v = values.at(c.rank);
    return (c.suit == trump_suit ? v.second : v.first);
}
// ===============================
//  BUILD CANNOT_BEAT AND BEAT SETS
// ===============================

// Построить множества cannot_beat_cards и beat_cards
// viewer — тот, для кого мы считаем (PLR или BOT)
// attacker — кто атакует в текущей волне
// defender — кто защищается
// st.table — список PairSlot
// trump_suit — масть козыря
//
// Возвращает pair<cannot_beat_cards, beat_cards>
std::pair<CardSet, CardSet> build_cannot_and_beat(
    const State &st,
    Side viewer,
    Side attacker,
    Side defender,
    int trump_suit)
{
    CardSet cannot;
    CardSet beat;

    // Считаем только если был защитный ход
    // (т.е. если actor == defender)
    if (st.actor != defender)
        return {cannot, beat};

    // attacker атаковал, defender защищался
    for (auto &slot : st.table)
    {
        const Card &atk = slot.attack;

        if (!slot.has_defense)
        {
            // НЕ отбил → cannot_beat_cards
            CardSet s = all_that_beat_or_equal(atk, trump_suit);
            cannot = set_union(cannot, s);
        }
        else
        {
            // Отбил → beat_cards
            const Card &def = slot.defense;

            CardSet s1 = all_that_beat_or_equal(atk, trump_suit);
            CardSet s2 = strictly_stronger(def, trump_suit);

            // beat_cards = s1 - s2
            CardSet b = set_diff(s1, s2);
            beat = set_union(beat, b);
        }
    }

    return {cannot, beat};
}
// -------------------------
// HELPERS
// -------------------------
//  полная колода 36 карт создание
vector<Card> make_full_deck_36()
{
    vector<Card> deck;
    deck.reserve(36);
    for (int suit = 0; suit < 4; ++suit)
    {
        for (int rank = 6; rank <= 14; ++rank)
        {
            deck.push_back(Card{rank, suit});
        }
    }
    return deck;
}

Side other(Side s) { return s == PLR ? BOT : PLR; }

int table_pairs(const State &st) { return (int)st.table.size(); }

int count_uncovered(const State &st)
{
    int c = 0;
    for (auto &p : st.table)
        if (!p.has_defense)
            c++;
    return c;
}
set<int> ranks_on_table(const State &st)
{
    set<int> s;
    for (auto &p : st.table)
    {
        s.insert(p.attack.rank);
        if (p.has_defense)
            s.insert(p.defense.rank);
    }
    return s;
}
bool deck_empty(const State &st) { return st.deck.empty(); }

bool is_trump(const Card &c, const Card &trump)
{
    return c.suit == trump.suit;
}

bool card_beats(const Card &d, const Card &a, const Card &trump)
{
    if (d.suit == a.suit)
        return d.rank > a.rank;
    if (is_trump(d, trump) && !is_trump(a, trump))
        return true;
    return false;
}

void remove_card(vector<Card> &hand, const Card &c)
{
    for (auto it = hand.begin(); it != hand.end(); ++it)
    {
        if (*it == c)
        {
            hand.erase(it);
            return;
        }
    }
}

void place_attack_card(vector<PairSlot> &table, const Card &c)
{
    PairSlot p;
    p.attack = c;
    p.has_defense = false;
    table.push_back(p);
}

void place_defense_card(vector<PairSlot> &table, const Card &c)
{
    for (auto &p : table)
    {
        if (!p.has_defense)
        {
            p.defense = c;
            p.has_defense = true;
            return;
        }
    }
}

void move_table_to_discard(State &st)
{
    for (auto &p : st.table)
    {
        st.discard.push_back(p.attack);
        if (p.has_defense)
            st.discard.push_back(p.defense);
    }
    st.table.clear();
}

void move_table_to_hand(State &st, Side s)
{
    auto &h = hand_of(st, s);
    for (auto &p : st.table)
    {
        h.push_back(p.attack);
        if (p.has_defense)
            h.push_back(p.defense);
    }
    st.table.clear();
}

void draw_up_to_6(State &st, Side s)
{
    auto &h = hand_of(st, s);

    while (h.size() < 6 && !st.deck.empty())
    {
        // добираем ВСЕГДА с ВЕРХА
        h.push_back(st.deck.front());
        st.deck.erase(st.deck.begin());
    }
}

// -------------------------
// PHASE DETECTION (what_ph)
// -------------------------

Phase what_ph(const State &st)
{
    Side A = st.attacker;
    Side X = st.actor;

    int pairs = table_pairs(st);
    int uncovered = count_uncovered(st);

    bool d_empty = deck_empty(st);
    bool plr_empty = st.hand_plr.empty();
    bool bot_empty = st.hand_bot.empty();

    // -----------------------------
    // GAMEOVER (только если стол пуст и колода пуста)
    // -----------------------------
    if (pairs == 0 && d_empty)
    {
        if (plr_empty && !bot_empty)
            return PH_GAMEOVER_PLR_WIN;
        if (!plr_empty && bot_empty)
            return PH_GAMEOVER_BOT_WIN;
        if (plr_empty && bot_empty)
            return PH_GAMEOVER_DRAW;
    }

    // -----------------------------
    // TABLE EMPTY (НЕ GAMEOVER)
    // -----------------------------
    if (pairs == 0)
    {
        if (X == A)
            return PH_ATTACK_START;
        return PH_INVALID_STATE; // по твоей спецификации
    }

    // -----------------------------
    // TABLE NOT EMPTY
    // -----------------------------
    if (X == A)
    {
        // ходит атакующий
        if (uncovered == 0)
            return PH_ATTACK_CONTINUE;
        return PH_EXTRA_ATTACK;
    }
    else
    {
        // ходит защитник
        if (uncovered == 0)
            return PH_END_OF_WAVE;
        if (uncovered == 1)
            return PH_DEFENSE_CONTINUE;
        return PH_EXTRA_ATTACK;
    }
}

// ===============================
//  UPDATE SETS — KNOWLEDGE MODEL CORE
// ===============================
void update_sets(State &st, const Move &m, UpdateContext ctx)
{
    Side actor = st.actor;
    Side attacker = st.attacker;
    Side defender = st.defender;
    int deck_size = st.deck.size();

    // Полная колода
    vector<Card> full = make_full_deck_36();

    // ---------------------------------------------------------
    // 1. known_in_opponent_hand
    // ---------------------------------------------------------

    // 1.1. actor сыграл карту → он точно её НЕ имеет
    if (!m.is_pass && ctx == UpdateContext::PLAY_CARD)
    {
        remove_from_set(st.known_in_opponent_hand[actor], m.card);
    }

    // 1.2. защитник взял стол → все карты со стола теперь точно у него
    if (ctx == UpdateContext::DEFENDER_TAKES)
    {
        for (int v = 0; v < 2; ++v)
        {
            Side viewer = (Side)v;
            Side opp = (viewer == PLR ? BOT : PLR);

            if (opp == defender)
                add_table_to_known(st, viewer);
        }
    }

    // 1.3. Эндшпиль: колода пуста или 1 карта
    if (deck_size <= 1)
    {
        st.known_in_opponent_hand[PLR].clear();
        for (auto &c : st.hand_bot)
            st.known_in_opponent_hand[PLR].insert(c);

        st.known_in_opponent_hand[BOT].clear();
        for (auto &c : st.hand_plr)
            st.known_in_opponent_hand[BOT].insert(c);
    }

    // ---------------------------------------------------------
    // 2. assumed_opponent_hand
    // ---------------------------------------------------------

    for (int v = 0; v < 2; ++v)
    {
        Side viewer = (Side)v;
        Side opp = (viewer == PLR ? BOT : PLR);

        auto &assumed = st.assumed_opponent_hand[v];
        assumed.clear();

        if (deck_size > 1)
        {
            // normal game
            for (auto &c : full)
                assumed.insert(c);

            // --- ВАЖНО: зеркальная логика ---
            if (viewer == BOT)
            {
                // BOT знает только свою руку
                for (auto &c : st.hand_bot)
                    remove_from_set(assumed, c);
            }
            else // viewer == PLR
            {
                // PLR знает только свою руку
                for (auto &c : st.hand_plr)
                    remove_from_set(assumed, c);
            }

            // - discard
            for (auto &c : st.discard)
                remove_from_set(assumed, c);

            // - table
            for (auto &slot : st.table)
            {
                remove_from_set(assumed, slot.attack);
                if (slot.has_defense)
                    remove_from_set(assumed, slot.defense);
            }

            // - trump (если колода ≥ 1)
            if (deck_size >= 1)
                remove_from_set(assumed, st.trump_card);
        }
        else
        {
            // endgame
            assumed.clear();
            for (auto &c : hand_of(st, opp))
                assumed.insert(c);
        }
    }
    // ---------------------------------------------------------
    // 3. assumed_not_in_opponent_hand  (упрощённая версия по силе)
    // ---------------------------------------------------------

    for (int v = 0; v < 2; ++v)
    {
        Side viewer = (Side)v;
        Side opp = (viewer == PLR ? BOT : PLR);

        // НЕ очищаем — накапливаем знания
        CardSet &ass_not = st.assumed_not_in_opponent_hand[v];

        if (deck_size > 1)
        {
            // Уточняем ТОЛЬКО если сейчас был защитный ход ОППОНЕНТА viewer
            if (ctx == UpdateContext::PLAY_CARD && defender == opp)
            {
                // Ищем пары атака/защита
                for (auto &slot : st.table)
                {
                    if (!slot.has_defense)
                        continue;

                    const Card &atk = slot.attack;
                    const Card &def = slot.defense;

                    double a = card_strength(atk, st.trump_card.suit);
                    double d = card_strength(def, st.trump_card.suit);

                    for (int suit = 0; suit < 4; ++suit)
                    {
                        for (int rank = 6; rank <= 14; ++rank)
                        {
                            Card c{rank, suit};

                            // карта должна уметь бить атаку (или быть самой атакой/защитой)
                            if (!(card_beats(c, atk, st.trump_card) || same_card(c, atk) || same_card(c, def)))
                                continue;

                            double s = card_strength(c, st.trump_card.suit);

                            if (s >= a && s <= d)
                                ass_not.insert(c);
                        }
                    }
                }
            }

            // Удаляем козырь
            ass_not.erase(st.trump_card);

            // Удаляем discard
            for (auto &c : st.discard)
                ass_not.erase(c);

            // Удаляем known_in_opponent_hand
            for (auto &c : st.known_in_opponent_hand[v])
                ass_not.erase(c);
        }
        else
        { // эндшпиль
            ass_not.clear();

            // 1. Рука viewer — точно не у оппонента
            for (auto &c : hand_of(st, viewer))
                ass_not.insert(c);

            // 2. Если в колоде 1 карта — это последний козырь, он ещё не разыгран
            if (deck_size == 1)
                ass_not.insert(st.trump_card);
        }
    }
    // ---------------------------------------------------------
    // 4. possible_in_opponent_hand
    // ---------------------------------------------------------

    for (int v = 0; v < 2; ++v)
    {
        Side viewer = (Side)v;
        Side opp = (viewer == PLR ? BOT : PLR);

        auto &possible = st.possible_in_opponent_hand[v];
        possible.clear();

        if (deck_size > 1)
        {
            // ВАЖНО: по ТЗ possible = assumed - assumed_not
            possible = set_diff(
                st.assumed_opponent_hand[v],
                st.assumed_not_in_opponent_hand[v]);
        }
        else
        {
            // Эндшпиль: possible = plr_hand / bot_hand (зеркально)
            for (auto &c : hand_of(st, opp))
                possible.insert(c);
        }
    }
}

// -------------------------
// VALIDATOR
// -------------------------

struct ResultMoves
{
    bool error;
    vector<Move> moves;

    static ResultMoves Err()
    {
        return {true, {}};
    }
    static ResultMoves Ok(const vector<Move> &m)
    {
        return {false, m};
    }
};

ResultMoves validator(const State &st)
{
    Phase ph = what_ph(st);
    Side A = st.attacker;
    Side D = st.defender;
    Side X = st.actor;

    int pairs = table_pairs(st);
    int uncovered = count_uncovered(st);

    // -------------------------
    // Фазы, где валидатор не вызывается
    // -------------------------
    if (ph == PH_GAMEOVER_PLR_WIN ||
        ph == PH_GAMEOVER_BOT_WIN ||
        ph == PH_GAMEOVER_DRAW)
        return ResultMoves::Err();

    if (ph == PH_END_OF_WAVE)
        return ResultMoves::Ok({Move::Pass()});

    vector<Move> moves;

    // -------------------------
    // ATTACK_START
    // -------------------------
    if (ph == PH_ATTACK_START)
    {
        if (X != A)
            return ResultMoves::Err();
        for (auto &c : hand_of(const_cast<State &>(st), A))
            moves.push_back(Move::Play(c));
        return ResultMoves::Ok(moves);
    }

    // -------------------------
    // DEFENSE_CONTINUE
    // -------------------------
    if (ph == PH_DEFENSE_CONTINUE)
    {
        if (X == A)
            return ResultMoves::Err();

        // Все карты, которые могут побить незакрытую атаку
        for (auto &p : st.table)
        {
            if (!p.has_defense)
            {
                for (auto &c : hand_of(const_cast<State &>(st), D))
                {
                    if (card_beats(c, p.attack, st.trump_card))
                        moves.push_back(Move::Play(c));
                }
            }
        }

        // PASS = взять (но фактически мы не берём сразу — это переход в EXTRA_ATTACK)
        moves.push_back(Move::Pass());

        // Убираем дубликаты
        vector<Move> uniq;
        for (auto &m : moves)
        {
            if (m.is_pass)
            {
                bool hasp = false;
                for (auto &u : uniq)
                    if (u.is_pass)
                        hasp = true;
                if (!hasp)
                    uniq.push_back(m);
            }
            else
            {
                bool found = false;
                for (auto &u : uniq)
                    if (!u.is_pass && u.card == m.card)
                        found = true;
                if (!found)
                    uniq.push_back(m);
            }
        }
        return ResultMoves::Ok(uniq);
    }

    // -------------------------
    // ATTACK_CONTINUE
    // -------------------------
    if (ph == PH_ATTACK_CONTINUE)
    {
        if (X != A)
            return ResultMoves::Err();
        if (ph == PH_ATTACK_CONTINUE)
        {
            if (X != A)
                return ResultMoves::Err();

            // === ЛИМИТ ПОДБРОСОВ (как в EXTRA_ATTACK) ===
            Side opp = other(A);
            int attacks_left =
                (int)hand_of(const_cast<State &>(st), opp).size() - uncovered;

            if (attacks_left <= 0)
            {
                // Подбрасывать нельзя — только PASS
                return ResultMoves::Ok({Move::Pass()});
            }

            // === обычная логика подбрасывания ===
            auto ranks = ranks_on_table(st);
            for (auto &c : hand_of(const_cast<State &>(st), A))
            {
                if (ranks.count(c.rank))
                    moves.push_back(Move::Play(c));
            }

            moves.push_back(Move::Pass());
            return ResultMoves::Ok(moves);
        }
        auto ranks = ranks_on_table(st);
        for (auto &c : hand_of(const_cast<State &>(st), A))
        {
            if (ranks.count(c.rank))
                moves.push_back(Move::Play(c));
        }

        // Атакер может сказать "бито"
        moves.push_back(Move::Pass());
        return ResultMoves::Ok(moves);
    }

    // -------------------------
    // EXTRA_ATTACK
    // -------------------------
    if (ph == PH_EXTRA_ATTACK)
    {

        if (X == A)
        {
            // Лимит подбросов
            Side opp = other(A);
            int attacks_left =
                (int)hand_of(const_cast<State &>(st), opp).size() - uncovered;

            if (attacks_left <= 0)
            {
                // Подбрасывать нельзя — только pass
                return ResultMoves::Ok({Move::Pass()});
            }

            auto ranks = ranks_on_table(st);
            for (auto &c : hand_of(const_cast<State &>(st), A))
            {
                if (ranks.count(c.rank))
                    moves.push_back(Move::Play(c));
            }

            moves.push_back(Move::Pass());
            return ResultMoves::Ok(moves);
        }

        // Защитник в EXTRA_ATTACK может только взять
        return ResultMoves::Ok({Move::Pass()});
    }

    return ResultMoves::Err();
}
// -------------------------
// APPLY_MOVE
// -------------------------

State apply_move(const State &s, const Move &m_in)
{
    State st = s;

    Side A = st.attacker;
    Side D = st.defender;
    Side X = st.actor;

    Phase ph = what_ph(st);

    // -------------------------
    // Валидация хода
    // -------------------------
    auto res = validator(st);
    if (res.error)
        return st;

    bool ok = false;
    for (auto &mv : res.moves)
    {
        if (mv.is_pass && m_in.is_pass)
            ok = true;
        if (!mv.is_pass && !m_in.is_pass && mv.card == m_in.card)
            ok = true;
    }
    if (!ok)
        return st;

    // -------------------------
    // ATTACK_START
    // -------------------------
    if (ph == PH_ATTACK_START)
    {
        if (X != A || m_in.is_pass)
            return st;

        remove_card(hand_of(st, A), m_in.card);
        place_attack_card(st.table, m_in.card);

        st.actor = D;
        update_sets(st, m_in, UpdateContext::PLAY_CARD);
        return st;
    }

    // -------------------------
    // ATTACK_CONTINUE
    // -------------------------
    if (ph == PH_ATTACK_CONTINUE)
    {
        if (X != A)
            return st;

        if (m_in.is_pass)
        {
            st.actor = D;
            update_sets(st, m_in, UpdateContext::PASS_NO_TAKE);
            return st;
        }

        remove_card(hand_of(st, A), m_in.card);
        place_attack_card(st.table, m_in.card);

        st.actor = D;
        update_sets(st, m_in, UpdateContext::PLAY_CARD);
        return st;
    }

    // -------------------------
    // DEFENSE_CONTINUE
    // -------------------------
    if (ph == PH_DEFENSE_CONTINUE)
    {
        if (X != D)
            return st;

        if (m_in.is_pass)
        {
            // Защитник сказал "беру" — но НЕ берёт сейчас.
            // Просто передаём ход атакеру → EXTRA_ATTACK.
            st.actor = A;
            update_sets(st, m_in, UpdateContext::PASS_NO_TAKE);
            return st;
        }

        // Защитник отбивает
        remove_card(hand_of(st, D), m_in.card);
        place_defense_card(st.table, m_in.card);

        st.actor = A;
        update_sets(st, m_in, UpdateContext::PLAY_CARD);
        return st;
    }

    // -------------------------
    // END_OF_WAVE
    // -------------------------
    if (ph == PH_END_OF_WAVE)
    {
        // PASS означает "завершить волну"

        move_table_to_discard(st);
        draw_up_to_6(st, A);
        draw_up_to_6(st, D);
        st.attacker = D;
        st.defender = A;
        st.actor = st.attacker;
        update_sets(st, m_in, UpdateContext::WAVE_END); // ← КОНТЕКСТ ВОЛНЫ
        return st;
    }

    // -------------------------
    // EXTRA_ATTACK
    // -------------------------
    if (ph == PH_EXTRA_ATTACK)
    {

        // ----- атакер -----
        if (X == A)
        {

            if (m_in.is_pass)
            {
                // 1-й вызов: фиксируем, что защитник забирает стол
                update_sets(st, m_in, UpdateContext::DEFENDER_TAKES);

                // Атакер закончил подбрасывать → защитник берёт всё
                move_table_to_hand(st, D);

                draw_up_to_6(st, A);
                draw_up_to_6(st, D);

                // Роли НЕ меняются
                st.actor = st.attacker;

                // 2-й вызов: конец волны
                update_sets(st, m_in, UpdateContext::WAVE_END);
                return st;
            }

            // Подброс картой
            remove_card(hand_of(st, A), m_in.card);
            place_attack_card(st.table, m_in.card);

            // По твоему требованию — атакер остаётся активным
            st.actor = A;

            update_sets(st, m_in, UpdateContext::PLAY_CARD);
            return st;
        }

        // ----- защитник -----
        // В EXTRA_ATTACK защитник уже пасанул → он может только взять
        if (m_in.is_pass)
        {
            // 1-й вызов: защитник забирает стол
            update_sets(st, m_in, UpdateContext::DEFENDER_TAKES);

            move_table_to_hand(st, D);

            draw_up_to_6(st, A);
            draw_up_to_6(st, D);

            st.actor = st.attacker;

            // 2-й вызов: конец волны
            update_sets(st, m_in, UpdateContext::WAVE_END);
            return st;
        }

        // Защитник НЕ может отбивать в EXTRA_ATTACK
        return st;
    }

    // -------------------------
    // GAMEOVER
    // -------------------------
    if (ph == PH_GAMEOVER_PLR_WIN ||
        ph == PH_GAMEOVER_BOT_WIN ||
        ph == PH_GAMEOVER_DRAW)
    {
        return st;
    }

    return st;
}
// -------------------------
// PRINT HELPERS
// -------------------------

string rank_to_str(int r)
{
    if (r >= 6 && r <= 10)
        return to_string(r);
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

string phase_to_str(Phase ph)
{
    switch (ph)
    {
    case PH_ATTACK_START:
        return "ATTACK_START";
    case PH_ATTACK_CONTINUE:
        return "ATTACK_CONTINUE";
    case PH_DEFENSE_CONTINUE:
        return "DEFENSE_CONTINUE";
    case PH_END_OF_WAVE:
        return "END_OF_WAVE";
    case PH_EXTRA_ATTACK:
        return "EXTRA_ATTACK";
    case PH_GAMEOVER_PLR_WIN:
        return "GAMEOVER_PLR_WIN";
    case PH_GAMEOVER_BOT_WIN:
        return "GAMEOVER_BOT_WIN";
    case PH_GAMEOVER_DRAW:
        return "GAMEOVER_DRAW";
    case PH_INVALID_STATE:
        return "INVALID_STATE";
    }
    return "UNKNOWN";
}

string suit_to_str(int s)
{
    const char *ss = "CDHS"; // Clubs, Diamonds, Hearts, Spades
    return string(1, ss[s]);
}

void print_card(const Card &c)
{
    cout << rank_to_str(c.rank) << suit_to_str(c.suit);
}

void print_set(const string &label, const CardSet &s)
{
    cout << label << ": ";
    for (auto &c : s)
    {
        print_card(c);
        cout << " ";
    }
    cout << "\n";
}

string side_to_str(Side s)
{
    return (s == PLR ? "PLR" : "BOT");
}

void print_state(const State &st)
{

    // --- МНОЖЕСТВА СО СТОРОНЫ BOT ---
    /*cout << "--- KNOWLEDGE FROM BOT PERSPECTIVE ---\n";

    print_set("known_in_opponent_hand[BOT]",
              st.known_in_opponent_hand[BOT]);

    print_set("assumed_opponent_hand[BOT]",
              st.assumed_opponent_hand[BOT]);

    print_set("assumed_not_in_opponent_hand[BOT]",
              st.assumed_not_in_opponent_hand[BOT]);

    print_set("possible_in_opponent_hand[BOT]",
              st.possible_in_opponent_hand[BOT]);

    cout << "\n";
    */
    cout << "==================== STATE ====================\n";
    // --- РОЛИ В ОДНУ СТРОКУ ---
    cout << "Roles: Attacker=" << side_to_str(st.attacker)
         << " Defender=" << side_to_str(st.defender)
         << " Actor=" << side_to_str(st.actor)
         << "\n\n";

    // --- КОЗЫРЬ + КОЛОДА + СБРОС ---
    cout << "Trump: ";
    print_card(st.trump_card);
    cout << " | Deck size: " << st.deck.size()
         << " | Discard: " << st.discard.size() << "\n";

    cout << "===============================================\n";

    // --- РУКИ ---
    cout << "PLR hand: ";
    for (auto &c : st.hand_plr)
    {
        print_card(c);
        cout << " ";
    }
    cout << "\n";

    cout << "BOT hand: ";
    for (auto &c : st.hand_bot)
    {
        print_card(c);
        cout << " ";
    }
    cout << "\n\n";

    // --- ИГРОВОЙ СТОЛ ---
    cout << "Table: ";
    if (st.table.empty())
    {
        cout << "(empty)";
    }
    else
    {
        for (auto &slot : st.table)
        {
            print_card(slot.attack);
            if (slot.has_defense)
            {
                cout << "/";
                print_card(slot.defense);
            }
            cout << "  ";
        }
    }
    cout << "\n\n";
}
//-----UX comand

void print_ux_diff(const State &old, const State &st)
{
    bool defender_took_table = false;

    // ---------------------------------------------------------
    // 0. STARTING DEAL
    // ---------------------------------------------------------
    if (old.hand_plr.empty() && old.hand_bot.empty() &&
        st.hand_plr.size() == 6 && st.hand_bot.size() == 6)
    {
        cout << "[UX] STARTING_DEAL\n";

        for (auto &c : st.hand_plr)
        {
            cout << "[UX] DEAL_CARD PLR ";
            print_card(c);
            cout << "\n";
        }

        for (auto &c : st.hand_bot)
        {
            cout << "[UX] DEAL_CARD BOT ";
            print_card(c);
            cout << "\n";
        }

        cout << "[UX] SET_TRUMP ";
        print_card(st.trump_card);
        cout << "\n";

        cout << "[UX] UPDATE_DECK_SIZE " << st.deck.size() << "\n";
        cout << "[UX] SET_ACTION_BUTTON NONE\n";
        return;
    }

    // ---------------------------------------------------------
    // 1. PLAY_ATTACK / PLAY_DEFENSE
    // ---------------------------------------------------------
    auto detect_who_played = [&]() -> Side
    {
        if (old.hand_plr.size() > st.hand_plr.size())
            return PLR;
        return BOT;
    };

    if (st.table.size() > old.table.size())
    {
        int idx = (int)st.table.size() - 1;
        const Card &c = st.table[idx].attack;

        Side who = detect_who_played();

        cout << "[UX] PLAY_ATTACK "
             << (who == PLR ? "PLR " : "BOT ");
        print_card(c);
        cout << " SLOT " << idx << "\n";
    }

    for (int i = 0; i < (int)st.table.size(); i++)
    {
        if (i < (int)old.table.size())
        {
            if (!old.table[i].has_defense && st.table[i].has_defense)
            {
                const Card &c = st.table[i].defense;
                Side who = detect_who_played();

                cout << "[UX] PLAY_DEFENSE "
                     << (who == PLR ? "PLR " : "BOT ");
                print_card(c);
                cout << " SLOT " << i << "\n";
            }
        }
    }

    // ---------------------------------------------------------
    // 2. TABLE_TO_HAND (защитник взял)
    // ---------------------------------------------------------
    if (!old.table.empty() && st.table.empty() &&
        old.discard.size() == st.discard.size())
    {
        defender_took_table = true;

        Side taker = old.defender;

        cout << "[UX] TABLE_TO_HAND "
             << (taker == PLR ? "PLR " : "BOT ");

        for (auto &p : old.table)
        {
            print_card(p.attack);
            cout << " ";
            if (p.has_defense)
            {
                print_card(p.defense);
                cout << " ";
            }
        }
        cout << "\n";
    }

    // ---------------------------------------------------------
    // 3. CLEAR_TABLE (отбой)
    // ---------------------------------------------------------
    if (!old.table.empty() && st.table.empty() &&
        old.discard.size() < st.discard.size())
    {
        cout << "[UX] CLEAR_TABLE ";
        for (auto &p : old.table)
        {
            print_card(p.attack);
            cout << " ";
            if (p.has_defense)
            {
                print_card(p.defense);
                cout << " ";
            }
        }
        cout << "\n";
    }

    // ---------------------------------------------------------
    // 4. UPDATE_DECK_SIZE (обычный)
    // ---------------------------------------------------------
    if (old.deck.size() != st.deck.size() &&
        st.deck.size() > 1)
    {
        cout << "[UX] UPDATE_DECK_SIZE " << st.deck.size() << "\n";
    }

    // ---------------------------------------------------------
    // 5. ОПРЕДЕЛЯЕМ ИСЧЕЗНУВШИЕ КАРТЫ ИЗ КОЛОДЫ
    // ---------------------------------------------------------
    CardSet old_deck = to_set(old.deck);
    CardSet new_deck = to_set(st.deck);
    CardSet removed = set_diff(old_deck, new_deck);

    bool removed_prev_trump = false;
    bool removed_trump = false;

    Card prev_trump;
    Card trump;

    if (!old.deck.empty())
    {
        trump = old.deck.back();
        if (old.deck.size() >= 2)
            prev_trump = old.deck[old.deck.size() - 2];
    }

    Side prev_trump_side = PLR;
    Side trump_side = PLR;

    auto who_got = [&](const Card &c) -> Side
    {
        if (to_set(st.hand_plr).count(c) &&
            !to_set(old.hand_plr).count(c))
            return PLR;
        return BOT;
    };

    if (removed.count(prev_trump))
    {
        removed_prev_trump = true;
        prev_trump_side = who_got(prev_trump);
    }

    if (removed.count(trump))
    {
        removed_trump = true;
        trump_side = who_got(trump);
    }

    // ---------------------------------------------------------
    // 6. DRAW UP TO 6 (обычный добор + PREVOISE_TRUMP + LAST_TRUMP)
    // ---------------------------------------------------------
    auto detect_draw_set = [&](Side s)
    {
        const auto &oldh = (s == PLR ? old.hand_plr : old.hand_bot);
        const auto &newh = (s == PLR ? st.hand_plr : st.hand_bot);

        CardSet old_set = to_set(oldh);
        CardSet new_set = to_set(newh);

        CardSet added = set_diff(new_set, old_set);
        if (added.empty())
            return;

        for (auto &c : added)
        {
            if (removed_prev_trump && c == prev_trump)
            {
                cout << "[UX] DEAL_PREVOISE_TRUMP "
                     << (s == PLR ? "PLR " : "BOT ");
                print_card(c);
                cout << "\n";
                continue;
            }

            if (removed_trump && c == trump)
            {
                cout << "[UX] DEAL_LAST_TRUMP "
                     << (s == PLR ? "PLR " : "BOT ");
                print_card(c);
                cout << "\n";
                continue;
            }

            cout << "[UX] DEAL_CARD " << (s == PLR ? "PLR " : "BOT ");
            print_card(c);
            cout << "\n";
        }
    };

    detect_draw_set(old.attacker);

    if (!defender_took_table)
        detect_draw_set(old.defender);

    // ---------------------------------------------------------
    // 7. UPDATE_DECK_SIZE 0 (когда колода кончилась)
    // ---------------------------------------------------------
    if (st.deck.empty() && !old.deck.empty())
    {
        cout << "[UX] UPDATE_DECK_SIZE 0\n";
    }

    // ---------------------------------------------------------
    // 8. GAMEOVER
    // ---------------------------------------------------------
    Phase ph_old = what_ph(old);
    Phase ph_new = what_ph(st);

    bool old_is_go =
        (ph_old == PH_GAMEOVER_PLR_WIN ||
         ph_old == PH_GAMEOVER_BOT_WIN ||
         ph_old == PH_GAMEOVER_DRAW);

    bool new_is_go =
        (ph_new == PH_GAMEOVER_PLR_WIN ||
         ph_new == PH_GAMEOVER_BOT_WIN ||
         ph_new == PH_GAMEOVER_DRAW);

    if (!old_is_go && new_is_go)
    {
        if (ph_new == PH_GAMEOVER_PLR_WIN)
            cout << "[UX] GAMEOVER PLR_WIN\n";
        else if (ph_new == PH_GAMEOVER_BOT_WIN)
            cout << "[UX] GAMEOVER BOT_WIN\n";
        else
            cout << "[UX] GAMEOVER DRAW\n";

        cout << "[UX] SET_ACTION_BUTTON NONE\n";
        return;
    }

    // ---------------------------------------------------------
    // 9. ACTION BUTTON
    // ---------------------------------------------------------
    Phase ph = what_ph(st);

    if (st.actor == BOT)
    {
        cout << "[UX] SET_ACTION_BUTTON NONE\n";
        return;
    }

    if (ph == PH_DEFENSE_CONTINUE)
        cout << "[UX] SET_ACTION_BUTTON TAKE\n";
    else if (ph == PH_ATTACK_CONTINUE)
        cout << "[UX] SET_ACTION_BUTTON BEAT\n";
    else if (ph == PH_EXTRA_ATTACK)
    {
        if (st.actor == st.attacker)
            cout << "[UX] SET_ACTION_BUTTON GIVE\n";
        else
            cout << "[UX] SET_ACTION_BUTTON TAKE\n";
    }
    else
        cout << "[UX] SET_ACTION_BUTTON NONE\n";
}

// -------------------------
// FULL GAME INITIALIZATION
// -------------------------

//   Пользуемся созданием колоды 36 карт
// vector<Card> make_full_deck_36()

// считаем, есть ли в руке 5+ карт одной масти
bool has_5_same_suit(const vector<Card> &hand)
{
    int cnt[4] = {0, 0, 0, 0};
    for (auto &c : hand)
        cnt[c.suit]++;
    for (int s = 0; s < 4; ++s)
        if (cnt[s] >= 5)
            return true;
    return false;
}

// находим младший козырь в руке; если нет — возвращаем +inf
int lowest_trump_rank(const vector<Card> &hand, int trump_suit)
{
    int best = 1000;
    for (auto &c : hand)
    {
        if (c.suit == trump_suit && c.rank < best)
            best = c.rank;
    }
    return best;
}

// инициализация первой партии
State init_first_game_state()
{
    State st;

    while (true)
    {
        // 1) создаём и мешаем полную колоду 36 карт
        vector<Card> full = make_full_deck_36();
        shuffle(full.begin(), full.end(), rng);

        // 2) раздаём по 6 карт с ВЕРХА колоды
        st.hand_plr.clear();
        st.hand_bot.clear();

        st.hand_plr.insert(st.hand_plr.end(), full.begin(), full.begin() + 6);
        st.hand_bot.insert(st.hand_bot.end(), full.begin() + 6, full.begin() + 12);

        // 3) оставшаяся колода (карты 13..36)
        vector<Card> deck(full.begin() + 12, full.end());

        // 4) проверяем «плохую раздачу» (5+ карт одной масти у кого-либо)
        if (has_5_same_suit(st.hand_plr) || has_5_same_suit(st.hand_bot))
        {
            // пересдаём
            continue;
        }

        // 5) козырь — последняя карта оставшейся колоды
        st.trump_card = deck.back();

        // 6) колода для игры: та же, козырь лежит внизу
        st.deck = deck;

        // 7) определяем, кто ходит (первая партия)
        int trump_suit = st.trump_card.suit;

        int plr_low_trump = lowest_trump_rank(st.hand_plr, trump_suit);
        int bot_low_trump = lowest_trump_rank(st.hand_bot, trump_suit);

        bool plr_has_trump = (plr_low_trump < 1000);
        bool bot_has_trump = (bot_low_trump < 1000);

        Side attacker;

        if (plr_has_trump && bot_has_trump)
        {
            if (plr_low_trump < bot_low_trump)
                attacker = PLR;
            else if (bot_low_trump < plr_low_trump)
                attacker = BOT;
            else
                attacker = (rng() % 2 == 0 ? PLR : BOT);
        }
        else if (plr_has_trump && !bot_has_trump)
        {
            attacker = PLR;
        }
        else if (!plr_has_trump && bot_has_trump)
        {
            attacker = BOT;
        }
        else
        {
            attacker = (rng() % 2 == 0 ? PLR : BOT);
        }

        st.attacker = attacker;
        st.defender = other(attacker);
        st.actor = attacker;

        st.table.clear();
        st.discard.clear();
        st.max_pairs = 6;

        return st;
    }
}
// -------------------------
// SIMPLE TEST INITIALIZATION
// -------------------------

State init_test_state()
{
    State st;
    // CDHS
    // Простейшая тестовая раздача
    st.attacker = PLR;
    st.defender = BOT;
    st.actor = PLR;

    // Трамп
    st.trump_card = {11, 2}; // J♥ например

    // Руки
    st.hand_plr = {
        {6, 0}};
    st.hand_bot = {
        {9, 2}, {10, 2}};

    // Колода (пустая для тестов)
    st.deck = {
        {11, 2}};

    return st;
}
//-------------------------
// EVAL
//-------------------------
// ---------------------------------------------------------
// COUNT RANKS IN HAND
// ---------------------------------------------------------
static inline int count_rank(const vector<Card> &h, int rank)
{
    int c = 0;
    for (auto &x : h)
        if (x.rank == rank)
            c++;
    return c;
}

// ---------------------------------------------------------
// FULL EVAL STATE — зеркальная, минимакс‑совместимая
// ---------------------------------------------------------
double eval_state(const State &st)
{
    const auto &bot = st.hand_bot;
    const auto &plr = st.hand_plr;

    int bot_cards = bot.size();
    int plr_cards = plr.size();
    int trump = st.trump_card.suit;

    // =========================================================
    // 1. ТЕРМИНАЛЬНЫЕ СОСТОЯНИЯ
    // =========================================================
    if (bot_cards == 0 && plr_cards > 0)
        return +8000; // бот выиграл
    if (plr_cards == 0 && bot_cards > 0)
        return -8000; // игрок выиграл
    if (bot_cards == 0 && plr_cards == 0)
        return +5000; // ничья = хорошо для бота

    double score = 0.0;

    // =========================================================
    // 2. НОРМАЛИЗОВАННАЯ СИЛА РУКИ
    // =========================================================
    double bot_sum = 0, plr_sum = 0;

    for (auto &c : bot)
        bot_sum += card_strength(c, trump);
    for (auto &c : plr)
        plr_sum += card_strength(c, trump);

    double bot_norm = (bot_cards > 0 ? bot_sum / bot_cards : 0);
    double plr_norm = (plr_cards > 0 ? plr_sum / plr_cards : 0);

    score += (bot_norm - plr_norm) * 20;

    // =========================================================
    // 3. ПАРНОСТЬ (пары/тройки/четвёрки)
    // =========================================================
    auto pair_score = [&](const vector<Card> &h)
    {
        map<int, int> cnt;
        for (auto &c : h)
            cnt[c.rank]++;

        int s = 0;
        for (auto &kv : cnt)
        {
            if (kv.second == 2)
                s += 2;
            if (kv.second == 3)
                s += 5;
            if (kv.second >= 4)
                s += 10;
        }
        return s;
    };

    score += pair_score(bot) * 5;
    score -= pair_score(plr) * 5;

    // =========================================================
    // 4. ИНИЦИАТИВА
    // =========================================================
    if (st.actor == BOT)
        score += 10;
    else
        score -= 10;

    // =========================================================
    // 5. FORCED PASS (симметрично!)
    // =========================================================
    auto res = validator(st);
    if (!res.error)
    {
        bool only_pass = true;
        for (auto &m : res.moves)
            if (!m.is_pass)
                only_pass = false;

        if (only_pass)
        {
            if (st.actor == BOT)
                score -= 10; // бот вынужден пасовать → плохо
            else
                score += 10; // игрок вынужден пасовать → хорошо
        }
    }

    // =========================================================
    // 6. MOBILITY (количество ходов)
    // =========================================================
    if (!res.error)
    {
        int bot_moves = 0, plr_moves = 0;

        if (st.actor == BOT)
            bot_moves = res.moves.size();
        else
        {
            State tmp = st;
            tmp.actor = PLR;
            auto r2 = validator(tmp);
            if (!r2.error)
                plr_moves = r2.moves.size();
        }

        score += (bot_moves - plr_moves) * 2;
    }

    return score;
}

// -------------------------
// BOT LOGIC
// -------------------------
bool is_endgame(const State &st)
{
    if (!st.deck.empty())
        return false;
    if (st.hand_bot.size() > 9)
        return false;
    if (st.hand_plr.size() > 9)
        return false;
    return true;
}
vector<Move> generate_moves(const State &st)
{
    auto res = validator(st);
    if (res.error)
        return {};
    return res.moves;
}

double minimax(State &st, int depth, double alpha, double beta, bool maximizingPlayer)
{
    if (depth == 0)
        return eval_state(st);

    Phase ph = what_ph(st);
    if (ph == PH_GAMEOVER_PLR_WIN ||
        ph == PH_GAMEOVER_BOT_WIN ||
        ph == PH_GAMEOVER_DRAW)
        return eval_state(st);

    auto moves = generate_moves(st);
    if (moves.empty())
        return eval_state(st);

    if (maximizingPlayer)
    {
        double best = -1e9;
        for (auto &m : moves)
        {
            State next = apply_move(st, m);
            bool nextMax = (next.actor == BOT);
            double val = minimax(next, depth - 1, alpha, beta, nextMax);
            best = max(best, val);
            alpha = max(alpha, val);
            if (beta <= alpha)
                break;
        }
        return best;
    }
    else
    {
        double best = 1e9;
        for (auto &m : moves)
        {
            State next = apply_move(st, m);
            bool nextMax = (next.actor == BOT);
            double val = minimax(next, depth - 1, alpha, beta, nextMax);
            best = min(best, val);
            beta = min(beta, val);
            if (beta <= alpha)
                break;
        }
        return best;
    }
}

Move choose_best_move_minimax(State &st, int depth)
{
    auto moves = generate_moves(st);
    if (moves.empty())
        return Move::Pass();

    double bestVal = -1e9;
    Move bestMove = moves[0];

    for (auto &m : moves)
    {
        State next = apply_move(st, m);
        bool nextMax = (next.actor == BOT);
        double val = minimax(next, depth - 1, -1e9, 1e9, nextMax);

        if (val > bestVal)
        {
            bestVal = val;
            bestMove = m;
        }
    }

    return bestMove;
}

Move midgame_choose(const State &st)
{
    cout << "EVAL BEFORE MOVE = " << eval_state(st) << "\n";
    auto res = validator(st);
    const auto &moves = res.moves;

    if (moves.empty())
    {
        Move m;
        m.is_pass = true;
        return m;
    }

    Side A = st.attacker;
    Side D = st.defender;
    Side X = st.actor;

    int trump = st.trump_card.suit;
    int deck_size = st.deck.size();

    // ---------------------------------------------------------
    // 1. DEFENSE — SMART LOGIC USING possible_in_opponent_hand
    // ---------------------------------------------------------
    if (X == D)
    {
        int trump = st.trump_card.suit;
        int deck_size = st.deck.size();

        // 0. Проверяем: все ли защитные ходы — только крупные козыри (Q,K,A)
        int defend_moves = 0;
        int big_trump_defends = 0;

        for (auto &m : moves)
        {
            if (m.is_pass)
                continue;

            defend_moves++;

            bool is_big_trump = (m.card.suit == trump && m.card.rank >= 12); // Q,K,A
            if (is_big_trump)
                big_trump_defends++;
        }

        bool has_only_big_trumps = (defend_moves > 0 && defend_moves == big_trump_defends);

        // 1. Если ТОЛЬКО крупные козыри и НЕ эндшпиль → берём
        //    (в эндшпиле у нас отдельная логика)
        if (has_only_big_trumps && deck_size > 1)
        {
            Move p;
            p.is_pass = true;
            return p;
        }

        // 2. Собираем ранги, которые возможны у оппонента
        std::set<int> opp_ranks;
        for (auto &c : st.possible_in_opponent_hand[BOT])
            opp_ranks.insert(c.rank);

        // 3. Ищем безопасные защиты (ранг отсутствует у оппонента)
        std::vector<Move> safe_moves;

        for (auto &m : moves)
        {
            if (m.is_pass)
                continue;

            if (!opp_ranks.count(m.card.rank))
                safe_moves.push_back(m);
        }

        // 4. Если есть безопасные — выбираем самую слабую
        if (!safe_moves.empty())
        {
            Move best = safe_moves[0];
            for (auto &m : safe_moves)
                if (card_strength(m.card, trump) < card_strength(best.card, trump))
                    best = m;
            return best;
        }

        // 5. Убираем крупные козыри (Q,K,A), если есть другие варианты
        std::vector<Move> filtered;

        bool has_non_big_trump = false;
        for (auto &m : moves)
        {
            if (m.is_pass)
                continue;

            bool is_big_trump = (m.card.suit == trump && m.card.rank >= 12);
            if (!is_big_trump)
                has_non_big_trump = true;
        }

        for (auto &m : moves)
        {
            if (m.is_pass)
                continue;

            bool is_big_trump = (m.card.suit == trump && m.card.rank >= 12);

            // если есть некрупные защиты — выкидываем крупные козыри
            if (has_non_big_trump && is_big_trump)
                continue;

            filtered.push_back(m);
        }

        // 6. Если остались ходы (некрупные или смесь, но случай "только крупные вне эндшпиля" уже отфильтрован выше)
        if (!filtered.empty())
        {
            Move best = filtered[0];
            for (auto &m : filtered)
                if (card_strength(m.card, trump) < card_strength(best.card, trump))
                    best = m;
            return best;
        }

        // 7. На всякий случай — если вдруг ничего не осталось, берём
        Move p;
        p.is_pass = true;
        return p;
    }
    // ---------------------------------------------------------
    // 2. ATTACK — SMART LOGIC USING possible_in_opponent_hand
    // ---------------------------------------------------------
    if (X == A)
    {
        const auto &possible = st.possible_in_opponent_hand[BOT];

        int deck_size = st.deck.size();
        bool is_first_attack = st.table.empty();

        // Проверка: есть ли некозыри
        bool has_non_trump_in_hand = false;
        for (auto &c : st.hand_bot)
            if (c.suit != trump)
                has_non_trump_in_hand = true;

        // Проверка: все ли карты — козыри
        bool all_trumps = true;
        for (auto &c : st.hand_bot)
            if (c.suit != trump)
                all_trumps = false;

        // ФУНКЦИЯ-ФИЛЬТР ДЛЯ ЛЮБОГО ЦИКЛА
        auto attack_filter = [&](const Move &m) -> bool
        {
            if (m.is_pass)
                return false;
            //---------------------------
            Phase ph = what_ph(st);
            bool defender_already_taking = (ph == PH_EXTRA_ATTACK);
            if (defender_already_taking)
                cout << " extra dobros!!! \n";
            if (defender_already_taking && m.card.suit == trump)
                return false; // не подбрасывать козырь когда противник берёт
            //--------------------------
            // 1. Запрет первой атаки козырем (если есть некозыри)
            if (is_first_attack && !all_trumps && m.card.suit == trump)
                return false;

            // 2. Запрет подбрасывать крупный козырь в НЕ-эндшпиле
            if (!is_first_attack && deck_size > 1 &&
                m.card.suit == trump && m.card.rank >= 12) // Q,K,A
                return false;

            return true;
        };

        // 1. Собрать масти, где у нас есть низкие карты (6–10)
        bool has_low_suit[4] = {false, false, false, false};
        for (auto &c : st.hand_bot)
            if (c.rank >= 6 && c.rank <= 10)
                has_low_suit[c.suit] = true;

        // 2. Посчитать количество карт каждой масти у оппонента
        int suit_count[4] = {0, 0, 0, 0};
        for (auto &c : possible)
            suit_count[c.suit]++;

        // 3. Выбрать масть, где у нас есть низкие карты, и у оппонента их меньше всего
        int best_suit = -1;
        int best_cnt = 100000;

        for (int s = 0; s < 4; ++s)
        {
            // запрет первой атаки козырем
            if (is_first_attack && !all_trumps && s == trump)
                continue;

            if (!has_low_suit[s])
                continue;

            if (suit_count[s] < best_cnt)
            {
                best_cnt = suit_count[s];
                best_suit = s;
            }
        }

        // 4. Если нашли масть с низкими картами — выбираем самую слабую низкую карту
        if (best_suit != -1)
        {
            Move best_move;
            bool found = false;

            for (auto &m : moves)
            {
                if (!attack_filter(m))
                    continue;
                if (m.card.suit != best_suit)
                    continue;
                if (m.card.rank < 6 || m.card.rank > 10)
                    continue;

                if (!found ||
                    card_strength(m.card, trump) < card_strength(best_move.card, trump))
                {
                    best_move = m;
                    found = true;
                }
            }

            if (found)
                return best_move;
        }

        // ---------------------------------------------------------
        // FALLBACK — если нет мастей с низкими картами
        // ---------------------------------------------------------

        // 5. Парные низкие (6–10)
        std::map<int, int> cnt;
        for (auto &c : st.hand_bot)
            cnt[c.rank]++;

        std::vector<Move> paired_low;
        for (auto &m : moves)
        {
            if (!attack_filter(m))
                continue;
            int r = m.card.rank;
            if (r >= 6 && r <= 10 && cnt[r] >= 2)
                paired_low.push_back(m);
        }

        if (!paired_low.empty())
        {
            Move best = paired_low[0];
            for (auto &m : paired_low)
                if (card_strength(m.card, trump) < card_strength(best.card, trump))
                    best = m;
            return best;
        }

        // 6. Любая некозырная 6–10
        bool found_non_trump_low = false;
        Move best_non_trump_low;

        for (auto &m : moves)
        {
            if (!attack_filter(m))
                continue;
            if (m.card.suit == trump)
                continue;
            if (m.card.rank < 6 || m.card.rank > 10)
                continue;

            if (!found_non_trump_low ||
                card_strength(m.card, trump) < card_strength(best_non_trump_low.card, trump))
            {
                best_non_trump_low = m;
                found_non_trump_low = true;
            }
        }

        if (found_non_trump_low)
            return best_non_trump_low;

        // 7. Если ВСЕ карты — козыри → атакуем самым слабым козырем
        if (!has_non_trump_in_hand)
        {
            Move best;
            bool found = false;

            for (auto &m : moves)
            {
                if (!attack_filter(m))
                    continue;

                if (!found ||
                    card_strength(m.card, trump) < card_strength(best.card, trump))
                {
                    best = m;
                    found = true;
                }
            }

            if (found)
                return best;
        }

        // 8. Иначе — слабая некозырная карта
        bool found_non_trump = false;
        Move best_non_trump;

        for (auto &m : moves)
        {
            if (!attack_filter(m))
                continue;
            if (m.card.suit == trump)
                continue;

            if (!found_non_trump ||
                card_strength(m.card, trump) < card_strength(best_non_trump.card, trump))
            {
                best_non_trump = m;
                found_non_trump = true;
            }
        }

        if (found_non_trump)
            return best_non_trump;

        // 9. Последний fallback — слабая карта
        Move best;
        bool found = false;

        for (auto &m : moves)
        {
            if (!attack_filter(m))
                continue;

            if (!found ||
                card_strength(m.card, trump) < card_strength(best.card, trump))
            {
                best = m;
                found = true;
            }
        }

        if (found)
            return best;

        // Если вообще ничего не нашли (теоретически невозможно)
        Move p;
        p.is_pass = true;
        return p;
    }
    // fallback
    return moves[rng() % moves.size()];
}
Move endgame_choose(State &st)
{
    cout << "=== MINIMAX ACTIVATED ===\n";

    int depth = 15; // нужен подбор
    double bestVal = -1e9;
    Move bestMove;

    auto moves = generate_moves(st);
    for (auto &m : moves)
    {
        State next = apply_move(st, m);
        bool nextMax = (next.actor == BOT);
        double val = minimax(next, depth - 1, -1e9, 1e9, nextMax);

        if (val > bestVal)
        {
            bestVal = val;
            bestMove = m;
        }
    }

    // --- ПРОГНОЗ ---
    if (bestVal >= 7000)
        cout << "===== Assumed BOT win   =====\n";
    else if (bestVal <= -7000)
        cout << "===== Assumed PLR win  =====\n";
    else
        cout << "=== NOT Assumed =========\n";

    return bestMove;
}
Move bot_choose(State &st)
{
    if (is_endgame(st))
        return endgame_choose(st);
    else
        return midgame_choose(st);
}

// -------------------------
// MAIN LOOP
// -------------------------

int main()
{
    srand(time(nullptr));

    State empty;
    State st = init_first_game_state();
    // print_ux_diff(empty, st);

    cout << "Init OK\n";
    // print_state(st);
    // cout << "Entering loop...\n";

    while (true)
    {
        print_state(st);

        Phase ph = what_ph(st);
        cout << "Phase: " << phase_to_str(ph) << "\n";

        // GAMEOVER — выводим и выходим
        if (ph == PH_GAMEOVER_PLR_WIN ||
            ph == PH_GAMEOVER_BOT_WIN ||
            ph == PH_GAMEOVER_DRAW)
        {
            cout << "GAME OVER: " << phase_to_str(ph) << "\n";
            break;
        }

        // AUTO END OF WAVE
        if (ph == PH_END_OF_WAVE)
        {
            cout << "[AUTO] End of wave\n";
            State old = st;
            st = apply_move(st, Move::Pass());
            // print_ux_diff(old, st);
            continue;
        }

        auto res = validator(st);
        if (res.error)
        {
            cout << "Validator error or invalid phase. Exiting.\n";
            break;
        }

        auto moves = res.moves;

        // AUTO: defender forced PASS in EXTRA_ATTACK
        if (ph == PH_EXTRA_ATTACK &&
            st.actor == st.defender &&
            moves.size() == 1 &&
            moves[0].is_pass)
        {
            cout << "[AUTO] Defender forced to take\n";
            State old = st;
            st = apply_move(st, moves[0]);
            // print_ux_diff(old, st);
            continue;
        }

        // AUTO: attacker cannot add more
        if (ph == PH_EXTRA_ATTACK &&
            st.actor == st.attacker &&
            moves.size() == 1 &&
            moves[0].is_pass)
        {
            cout << "[AUTO] Attacker cannot add\n";
            State old = st;
            st = apply_move(st, moves[0]);
            // print_ux_diff(old, st);
            continue;
        }

        // BOT MOVE
        if (st.actor == BOT)
        {
            Move m = bot_choose(st);
            cout << "[BOT] chooses: ";
            if (m.is_pass)
                cout << "PASS\n";
            else
            {
                print_card(m.card);
                cout << "\n";
            }
            State old = st;
            st = apply_move(st, m);
            // print_ux_diff(old, st);
            continue;
        }

        // PLAYER MOVE
        cout << "Valid moves:\n";
        for (int i = 0; i < (int)moves.size(); i++)
        {
            cout << i << ": ";
            if (moves[i].is_pass)
                cout << "PASS\n";
            else
            {
                print_card(moves[i].card);
                cout << "\n";
            }
        }

        cout << "Enter move index (or q): ";
        string line;
        if (!getline(cin, line))
            break;
        if (line == "q")
            break;

        int idx = -1;
        try
        {
            idx = stoi(line);
        }
        catch (...)
        {
            continue;
        }

        if (idx < 0 || idx >= (int)moves.size())
        {
            cout << "Invalid index\n";
            continue;
        }

        Move chosen = moves[idx];
        State old = st;
        st = apply_move(st, chosen);
        print_ux_diff(old, st);
        Phase next_ph = what_ph(st);
        if (next_ph == PH_GAMEOVER_PLR_WIN ||
            next_ph == PH_GAMEOVER_BOT_WIN ||
            next_ph == PH_GAMEOVER_DRAW)
        {
            print_state(st);
            cout << "GAME OVER: " << next_ph << "\n";
            break;
        }
    }

    return 0;
}
