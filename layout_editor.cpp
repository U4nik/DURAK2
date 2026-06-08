#include <SFML/Graphics.hpp>
#include <iostream>
#include <vector>
#include <string>
#include <fstream>

using namespace std;

// =========================
//   CARD SPRITE
// =========================
struct CardSprite
{
    sf::Sprite sprite;
    bool faceUp = false;
    float angle = 0.f;
};

// =========================
//   BUTTON SPRITE
// =========================
struct ButtonSprite
{
    sf::Sprite sprite;
    std::string id; // "action", "yes", "no"
};

// =========================
//   DIALOG WINDOW
// =========================
struct DialogWindow
{
    bool exists = false;
    bool resizing = false;

    float x = 0.f;
    float y = 0.f;
    float w = 600.f;
    float h = 300.f;

    std::wstring text = L"Продолжать игру?";
};

// =========================
//   EDIT MODES
// =========================
enum class EditMode
{
    NONE,
    BOT_HAND,
    PLR_HAND,
    ATTACK_SLOTS,
    DECK,
    TRUMP,
    BUTTON_ACTION, // 7
    BUTTON_YESNO,  // 8
    DIALOG_WINDOW, // 9
    VIEW_ALL
};

// =========================
//   LAYER
// =========================
struct Layer
{
    vector<CardSprite> cards;
    vector<ButtonSprite> buttons;
    bool visible = true;
};

int main()
{
    const std::string cardsPath = "F:/MYPROG/DURAK2/cards/";
    const std::string buttonsPath = "F:/MYPROG/DURAK2/buttons/";

    sf::RenderWindow window(sf::VideoMode(1920, 1080),
                            "Durak Layout Editor",
                            sf::Style::Fullscreen);
    window.setVerticalSyncEnabled(true);

    // =========================
    //   TEXTURES
    // =========================
    sf::Texture texBackground;
    sf::Texture texBack;
    sf::Texture texFaceDummy;

    sf::Texture texAction;
    sf::Texture texYes;
    sf::Texture texNo;

    if (!texBackground.loadFromFile(cardsPath + "background.png"))
        return 1;
    if (!texBack.loadFromFile(cardsPath + "back.png"))
        return 1;
    if (!texFaceDummy.loadFromFile(cardsPath + "10h.png"))
        return 1;

    if (!texAction.loadFromFile(buttonsPath + "action_btn_beru.png"))
        return 1;
    if (!texYes.loadFromFile(buttonsPath + "yes_btn.png"))
        return 1;
    if (!texNo.loadFromFile(buttonsPath + "no_btn.png"))
        return 1;

    sf::Sprite background(texBackground);

    // =========================
    //   FONT
    // =========================
    sf::Font font;
    if (!font.loadFromFile("Symbola-AjYx.ttf"))
        return 1;

    sf::Text uiText;
    uiText.setFont(font);
    uiText.setCharacterSize(28);
    uiText.setFillColor(sf::Color::White);
    uiText.setPosition(20.f, 1000.f);
    uiText.setString(L"Нажмите M для выбора режима");

    // =========================
    //   LAYERS
    // =========================
    Layer layerBotHand;
    Layer layerPlrHand;
    Layer layerAttack;
    Layer layerDefense;
    Layer layerDeck;
    Layer layerTrump;
    Layer layerButtons;

    // =========================
    //   DIALOG WINDOW INSTANCE
    // =========================
    DialogWindow dialog;

    // =========================
    //   MODE
    // =========================
    EditMode mode = EditMode::NONE;

    // =========================
    //   ACTIVE CARD
    // =========================
    CardSprite activeCard;
    activeCard.sprite.setTexture(texBack);
    activeCard.sprite.setOrigin(62.f, 90.f);

    auto resetActiveCard = [&]()
    {
        activeCard.faceUp = false;
        activeCard.angle = 0.f;
        activeCard.sprite.setTexture(texBack);
        activeCard.sprite.setOrigin(62.f, 90.f);
        activeCard.sprite.setRotation(0.f);
        activeCard.sprite.setPosition(1920.f / 2.f, 1080.f / 2.f);
    };

    resetActiveCard();

    // =========================
    //   ACTIVE MARKER (POINT)
    // =========================
    sf::CircleShape activeMarker(8.f);
    activeMarker.setFillColor(sf::Color::Black);
    activeMarker.setOrigin(8.f, 8.f);
    activeMarker.setPosition(1920.f / 2.f, 1080.f / 2.f);

    // =========================
    //   DRAGGING
    // =========================
    bool dragging = false;
    sf::Vector2f dragOffset;

    // =========================
    //   HAND INPUT (NEW SYSTEM)
    // =========================
    bool handInputActive = false;
    int handCount = 6;
    const int HAND_MIN = 1;
    const int HAND_MAX = 20;

    // =========================
    //   ATTACK/DEFENSE SLOTS
    // =========================
    int pendingSlotStep = 0;
    sf::Vector2f A1, A2, D1;

    // =========================
    //   YES/NO BUTTONS
    // =========================
    bool waitingForYes = false;
    bool waitingForNo = false;

    const float YES_NO_OFFSET = 220.f; // больше не используется, но оставим

    // =========================
    //   SAVE LAYOUT (JSON)
    // =========================
    auto save_layout = [&]()
    {
        std::ofstream f("layout_game.json");
        if (!f.is_open())
        {
            uiText.setString(L"Ошибка: не могу открыть layout_game.json");
            return;
        }

        f << "{\n";

        // ACTION BUTTON
        sf::Vector2f actionPos;
        bool hasAction = false;
        for (auto &b : layerButtons.buttons)
        {
            if (b.id == "action")
            {
                actionPos = b.sprite.getPosition();
                hasAction = true;
                break;
            }
        }
        if (hasAction)
        {
            f << "  \"action_button\": { \"x\": " << actionPos.x
              << ", \"y\": " << actionPos.y << " },\n";
        }

        // YES / NO
        sf::Vector2f yesPos, noPos;
        bool hasYes = false, hasNo = false;
        for (auto &b : layerButtons.buttons)
        {
            if (b.id == "yes")
            {
                yesPos = b.sprite.getPosition();
                hasYes = true;
            }
            if (b.id == "no")
            {
                noPos = b.sprite.getPosition();
                hasNo = true;
            }
        }
        if (hasYes)
        {
            f << "  \"yes_button\": { \"x\": " << yesPos.x
              << ", \"y\": " << yesPos.y << " },\n";
        }
        if (hasNo)
        {
            f << "  \"no_button\": { \"x\": " << noPos.x
              << ", \"y\": " << noPos.y << " },\n";
        }

        // DIALOG
        if (dialog.exists)
        {
            f << "  \"dialog\": {\n";
            f << "    \"x\": " << dialog.x << ",\n";
            f << "    \"y\": " << dialog.y << ",\n";
            f << "    \"w\": " << dialog.w << ",\n";
            f << "    \"h\": " << dialog.h << ",\n";
            f << "    \"text\": \"Продолжать игру?\"";

            if (hasYes || hasNo)
                f << ",\n";
            else
                f << "\n";

            if (hasYes)
            {
                f << "    \"yes_button\": { \"x\": " << yesPos.x
                  << ", \"y\": " << yesPos.y << " }";
                if (hasNo)
                    f << ",\n";
                else
                    f << "\n";
            }
            if (hasNo)
            {
                f << "    \"no_button\": { \"x\": " << noPos.x
                  << ", \"y\": " << noPos.y << " }\n";
            }

            f << "  },\n";
        }

        // BOT HAND
        if (!layerBotHand.cards.empty())
        {
            f << "  \"bot_hand\": [\n";
            for (size_t i = 0; i < layerBotHand.cards.size(); i++)
            {
                auto &c = layerBotHand.cards[i];
                f << "    { \"x\": " << c.sprite.getPosition().x
                  << ", \"y\": " << c.sprite.getPosition().y << " }";
                if (i + 1 < layerBotHand.cards.size())
                    f << ",";
                f << "\n";
            }
            f << "  ],\n";
        }

        // PLAYER HAND
        if (!layerPlrHand.cards.empty())
        {
            f << "  \"player_hand\": [\n";
            for (size_t i = 0; i < layerPlrHand.cards.size(); i++)
            {
                auto &c = layerPlrHand.cards[i];
                f << "    { \"x\": " << c.sprite.getPosition().x
                  << ", \"y\": " << c.sprite.getPosition().y << " }";
                if (i + 1 < layerPlrHand.cards.size())
                    f << ",";
                f << "\n";
            }
            f << "  ],\n";
        }

        // ATTACK SLOTS
        if (!layerAttack.cards.empty())
        {
            f << "  \"attack_slots\": [\n";
            for (size_t i = 0; i < layerAttack.cards.size(); i++)
            {
                auto &c = layerAttack.cards[i];
                f << "    { \"x\": " << c.sprite.getPosition().x
                  << ", \"y\": " << c.sprite.getPosition().y << " }";
                if (i + 1 < layerAttack.cards.size())
                    f << ",";
                f << "\n";
            }
            f << "  ],\n";
        }

        // DEFENSE SLOTS
        if (!layerDefense.cards.empty())
        {
            f << "  \"defense_slots\": [\n";
            for (size_t i = 0; i < layerDefense.cards.size(); i++)
            {
                auto &c = layerDefense.cards[i];
                f << "    { \"x\": " << c.sprite.getPosition().x
                  << ", \"y\": " << c.sprite.getPosition().y << " }";
                if (i + 1 < layerDefense.cards.size())
                    f << ",";
                f << "\n";
            }
            f << "  ],\n";
        }

        // DECK
        if (!layerDeck.cards.empty())
        {
            auto &c = layerDeck.cards[0];
            f << "  \"deck\": { \"x\": " << c.sprite.getPosition().x
              << ", \"y\": " << c.sprite.getPosition().y
              << ", \"angle\": " << c.angle
              << " },\n";
        }

        // TRUMP
        if (!layerTrump.cards.empty())
        {
            auto &c = layerTrump.cards[0];
            f << "  \"trump\": { \"x\": " << c.sprite.getPosition().x
              << ", \"y\": " << c.sprite.getPosition().y
              << ", \"angle\": " << c.angle
              << " }\n";
        }

        f << "}\n";
        f.close();

        uiText.setString(L"layout_game.json сохранён");
    };
    //=============
    // LOAD
    //=============
    auto load_layout = [&]()
    {
        std::ifstream f("layout_game.json");
        if (!f.is_open())
        {
            uiText.setString(L"Ошибка: нет layout_game.json");
            return;
        }

        std::string json((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());

        // ===== безопасный поиск значения внутри блока =====
        auto getVal = [&](const std::string &key, size_t startPos) -> float
        {
            size_t p = json.find(key, startPos);
            if (p == std::string::npos)
                return 0.f;

            p = json.find(":", p);
            if (p == std::string::npos)
                return 0.f;

            size_t end = json.find_first_of(",}", p + 1);
            if (end == std::string::npos)
                end = json.size();

            return std::stof(json.substr(p + 1, end - (p + 1)));
        };

        // ===== очистка всех слоёв =====
        layerButtons.buttons.clear();
        layerDeck.cards.clear();
        layerTrump.cards.clear();
        layerBotHand.cards.clear();
        layerPlrHand.cards.clear();
        layerAttack.cards.clear();
        layerDefense.cards.clear();
        dialog.exists = false;

        // ============================================================
        //   LOAD DECK
        // ============================================================
        size_t deckPos = json.find("\"deck\"");
        if (deckPos != std::string::npos)
        {
            CardSprite c;
            c.sprite.setTexture(texBack);
            c.sprite.setOrigin(62.f, 90.f);

            float x = getVal("\"x\"", deckPos);
            float y = getVal("\"y\"", deckPos);
            float angle = getVal("\"angle\"", deckPos);

            c.sprite.setPosition(x, y);
            c.angle = angle;
            c.sprite.setRotation(angle);

            layerDeck.cards.push_back(c);
        }

        // ============================================================
        //   LOAD TRUMP
        // ============================================================
        size_t trumpPos = json.find("\"trump\"");
        if (trumpPos != std::string::npos)
        {
            CardSprite c;
            c.sprite.setTexture(texFaceDummy);
            c.sprite.setOrigin(62.f, 90.f);

            float x = getVal("\"x\"", trumpPos);
            float y = getVal("\"y\"", trumpPos);
            float angle = getVal("\"angle\"", trumpPos);

            c.sprite.setPosition(x, y);
            c.angle = angle;
            c.sprite.setRotation(angle);

            layerTrump.cards.push_back(c);
        }

        // ============================================================
        //   LOAD ACTION BUTTON
        // ============================================================
        size_t actionPos = json.find("\"action_button\"");
        if (actionPos != std::string::npos)
        {
            ButtonSprite b;
            b.id = "action";
            b.sprite.setTexture(texAction);
            b.sprite.setOrigin(texAction.getSize().x / 2.f, texAction.getSize().y / 2.f);

            float x = getVal("\"x\"", actionPos);
            float y = getVal("\"y\"", actionPos);

            b.sprite.setPosition(x, y);
            layerButtons.buttons.push_back(b);
        }

        // ============================================================
        //   LOAD YES BUTTON
        // ============================================================
        bool hasYes = false;
        sf::Vector2f yesPos;

        size_t yesBlock = json.find("\"yes_button\"");
        if (yesBlock != std::string::npos)
        {
            ButtonSprite b;
            b.id = "yes";
            b.sprite.setTexture(texYes);
            b.sprite.setOrigin(texYes.getSize().x / 2.f, texYes.getSize().y / 2.f);

            float x = getVal("\"x\"", yesBlock);
            float y = getVal("\"y\"", yesBlock);

            b.sprite.setPosition(x, y);
            yesPos = {x, y};
            hasYes = true;

            layerButtons.buttons.push_back(b);
        }

        // ============================================================
        //   LOAD NO BUTTON
        // ============================================================
        bool hasNo = false;
        sf::Vector2f noPos;

        size_t noBlock = json.find("\"no_button\"");
        if (noBlock != std::string::npos)
        {
            ButtonSprite b;
            b.id = "no";
            b.sprite.setTexture(texNo);
            b.sprite.setOrigin(texNo.getSize().x / 2.f, texNo.getSize().y / 2.f);

            float x = getVal("\"x\"", noBlock);
            float y = getVal("\"y\"", noBlock);

            noPos = {x, y};
            hasNo = true;

            layerButtons.buttons.push_back(b);
        }

        // === выравнивание NO по YES ===
        if (hasYes && hasNo)
        {
            noPos.y = yesPos.y;

            for (auto &b : layerButtons.buttons)
            {
                if (b.id == "no")
                    b.sprite.setPosition(noPos);
            }
        }

        // ============================================================
        //   UNIVERSAL ARRAY LOADER
        // ============================================================
        auto loadCardArray = [&](const std::string &key, Layer &layer, bool faceUp)
        {
            size_t pos = json.find(key);
            if (pos == std::string::npos)
                return;

            size_t arrStart = json.find("[", pos);
            size_t arrEnd = json.find("]", arrStart);
            if (arrStart == std::string::npos || arrEnd == std::string::npos)
                return;

            std::string arr = json.substr(arrStart, arrEnd - arrStart);

            size_t p = 0;
            while (true)
            {
                size_t xKey = arr.find("\"x\"", p);
                if (xKey == std::string::npos)
                    break;

                size_t xColon = arr.find(":", xKey);
                size_t xEnd = arr.find(",", xColon);
                float x = std::stof(arr.substr(xColon + 1, xEnd - (xColon + 1)));

                size_t yKey = arr.find("\"y\"", xEnd);
                size_t yColon = arr.find(":", yKey);
                size_t yEnd = arr.find_first_of(",}", yColon);
                float y = std::stof(arr.substr(yColon + 1, yEnd - (yColon + 1)));

                CardSprite c;
                c.sprite.setOrigin(62.f, 90.f);
                c.sprite.setPosition(x, y);

                if (faceUp)
                    c.sprite.setTexture(texFaceDummy);
                else
                    c.sprite.setTexture(texBack);

                layer.cards.push_back(c);

                p = yEnd;
            }
        };

        // ============================================================
        //   LOAD HANDS AND SLOTS
        // ============================================================
        loadCardArray("\"bot_hand\"", layerBotHand, false);
        loadCardArray("\"player_hand\"", layerPlrHand, true);
        loadCardArray("\"attack_slots\"", layerAttack, true);
        loadCardArray("\"defense_slots\"", layerDefense, true);

        // ============================================================
        //   LOAD DIALOG WINDOW
        // ============================================================
        size_t dialogPos = json.find("\"dialog\"");
        if (dialogPos != std::string::npos)
        {
            dialog.exists = true;

            dialog.x = getVal("\"x\"", dialogPos);
            dialog.y = getVal("\"y\"", dialogPos);
            dialog.w = getVal("\"w\"", dialogPos);
            dialog.h = getVal("\"h\"", dialogPos);
        }

        uiText.setString(L"JSON загружен");
    };

    // =========================
    //   MAIN LOOP
    // =========================
    while (window.isOpen())
    {
        sf::Event event;
        while (window.pollEvent(event))
        {
            if (event.type == sf::Event::Closed)
                window.close();

            // KEY PRESSED
            if (event.type == sf::Event::KeyPressed)
            {
                if (event.key.code == sf::Keyboard::L)
                {
                    load_layout();
                }

                if (event.key.code == sf::Keyboard::Escape)
                    window.close();

                if (event.key.code == sf::Keyboard::S)
                    save_layout();

                if (event.key.code == sf::Keyboard::M)
                {
                    uiText.setString(
                        L"1-Бот, 2-Игрок, 3-Слоты, 5-Колода, 6-Козырь, "
                        L"7-Action, 8-Yes/No, 9-Dialog");
                }

                if (event.key.code == sf::Keyboard::Num7)
                {
                    mode = EditMode::BUTTON_ACTION;
                    handInputActive = false;
                    dialog.resizing = false;
                    // === ПАТЧ 2: удаляем старую эктион кнопку ===
                    layerButtons.buttons.erase(
                        std::remove_if(
                            layerButtons.buttons.begin(),
                            layerButtons.buttons.end(),
                            [](const ButtonSprite &b)
                            {
                                return b.id == "action";
                            }),
                        layerButtons.buttons.end());

                    uiText.setString(L"Action button: поставьте точку и нажмите F");
                }

                if (event.key.code == sf::Keyboard::Num8)
                {
                    mode = EditMode::BUTTON_YESNO;
                    handInputActive = false;
                    dialog.resizing = false;
                    // === ПАТЧ 1: удаляем старые yes/no ===
                    layerButtons.buttons.erase(
                        std::remove_if(
                            layerButtons.buttons.begin(),
                            layerButtons.buttons.end(),
                            [](const ButtonSprite &b)
                            {
                                return b.id == "yes" || b.id == "no";
                            }),
                        layerButtons.buttons.end());

                    waitingForYes = true;
                    waitingForNo = false;

                    uiText.setString(L"Режим Yes/No: поставьте точку для ДА и нажмите F");
                }

                if (event.key.code == sf::Keyboard::Num9)
                {
                    mode = EditMode::DIALOG_WINDOW;
                    handInputActive = false;

                    dialog.exists = false;
                    dialog.resizing = false;

                    uiText.setString(L"Диалоговое окно: поставьте левый верх и нажмите F");
                }

                if (event.key.code == sf::Keyboard::Num1)
                {
                    mode = EditMode::BOT_HAND;
                    handInputActive = true;
                    handCount = 6;

                    uiText.setString(
                        L"Рука бота: выберите Y активной карты, затем ←/→ или +/- (" +
                        std::to_wstring(handCount) +
                        L"), Enter — подтвердить");

                    resetActiveCard();
                }

                if (event.key.code == sf::Keyboard::Num2)
                {
                    mode = EditMode::PLR_HAND;
                    handInputActive = true;
                    handCount = 6;

                    uiText.setString(
                        L"Рука игрока: выберите Y активной карты, затем ←/→ или +/- (" +
                        std::to_wstring(handCount) +
                        L"), Enter — подтвердить");

                    resetActiveCard();
                }

                if (event.key.code == sf::Keyboard::Num3)
                {
                    mode = EditMode::ATTACK_SLOTS;
                    handInputActive = false;
                    dialog.resizing = false;

                    // == = ОЧИСТКА СТАРЫХ СЛОТОВ ==
                    layerAttack.cards.clear();
                    layerDefense.cards.clear();
                    pendingSlotStep = 0;
                    uiText.setString(L"Слоты: поставьте A1 и нажмите F");

                    resetActiveCard();
                }

                if (event.key.code == sf::Keyboard::Num5)
                {
                    mode = EditMode::DECK;
                    handInputActive = false;
                    dialog.resizing = false;

                    uiText.setString(L"Колода: поставьте карту и нажмите F");
                    resetActiveCard();
                }

                if (event.key.code == sf::Keyboard::Num6)
                {
                    mode = EditMode::TRUMP;
                    handInputActive = false;
                    dialog.resizing = false;

                    uiText.setString(L"Козырь: поставьте карту и нажмите F");
                    resetActiveCard();
                }

                // HAND COUNT INPUT
                if (handInputActive &&
                    (mode == EditMode::BOT_HAND || mode == EditMode::PLR_HAND))
                {
                    bool changed = false;

                    if (event.key.code == sf::Keyboard::Left ||
                        event.key.code == sf::Keyboard::Subtract)
                    {
                        handCount--;
                        changed = true;
                    }
                    if (event.key.code == sf::Keyboard::Right ||
                        event.key.code == sf::Keyboard::Add)
                    {
                        handCount++;
                        changed = true;
                    }

                    if (changed)
                    {
                        if (handCount < HAND_MIN)
                            handCount = HAND_MIN;
                        if (handCount > HAND_MAX)
                            handCount = HAND_MAX;

                        std::wstring who = (mode == EditMode::BOT_HAND)
                                               ? L"Рука бота: "
                                               : L"Рука игрока: ";

                        uiText.setString(
                            who +
                            L"выберите Y активной карты, затем ←/→ или +/- (" +
                            std::to_wstring(handCount) +
                            L"), Enter — подтвердить");
                    }

                    if (event.key.code == sf::Keyboard::Enter)
                    {
                        float W = 124.f, H = 180.f, gap = 20.f;
                        int N = handCount;

                        float total = N * W + (N - 1) * gap;
                        float x0 = (1920.f - total) / 2.f;
                        float Y = activeCard.sprite.getPosition().y;

                        if (mode == EditMode::BOT_HAND)
                        {
                            layerBotHand.cards.clear();
                            for (int i = 0; i < N; i++)
                            {
                                CardSprite c;
                                c.sprite.setTexture(texBack);
                                c.sprite.setOrigin(62.f, 90.f);
                                c.sprite.setPosition(x0 + i * (W + gap), Y);
                                layerBotHand.cards.push_back(c);
                            }
                            uiText.setString(L"Рука бота построена");
                        }
                        else
                        {
                            layerPlrHand.cards.clear();
                            for (int i = 0; i < N; i++)
                            {
                                CardSprite c;
                                c.sprite.setTexture(texFaceDummy);
                                c.sprite.setOrigin(62.f, 90.f);
                                c.sprite.setPosition(x0 + i * (W + gap), Y);
                                layerPlrHand.cards.push_back(c);
                            }
                            uiText.setString(L"Рука игрока построена");
                        }

                        handInputActive = false;
                        resetActiveCard();
                    }

                    continue;
                }

                // DIALOG WINDOW RESIZING
                if (dialog.resizing && mode == EditMode::DIALOG_WINDOW)
                {
                    bool changed = false;

                    if (event.key.code == sf::Keyboard::Left)
                    {
                        dialog.w -= 20.f;
                        changed = true;
                    }
                    if (event.key.code == sf::Keyboard::Right)
                    {
                        dialog.w += 20.f;
                        changed = true;
                    }
                    if (event.key.code == sf::Keyboard::Up)
                    {
                        dialog.h -= 20.f;
                        changed = true;
                    }
                    if (event.key.code == sf::Keyboard::Down)
                    {
                        dialog.h += 20.f;
                        changed = true;
                    }

                    if (changed)
                    {
                        if (dialog.w < 200.f)
                            dialog.w = 200.f;
                        if (dialog.w > 1200.f)
                            dialog.w = 1200.f;
                        if (dialog.h < 150.f)
                            dialog.h = 150.f;
                        if (dialog.h > 800.f)
                            dialog.h = 800.f;

                        uiText.setString(
                            L"Размер окна: " +
                            std::to_wstring((int)dialog.w) +
                            L" × " +
                            std::to_wstring((int)dialog.h) +
                            L"   (←/→ ширина, ↑/↓ высота, Enter — подтвердить)");
                    }

                    if (event.key.code == sf::Keyboard::Enter)
                    {
                        dialog.resizing = false;
                        uiText.setString(
                            L"Окно зафиксировано. Теперь разместите кнопки ДА и НЕТ (режим 8).");
                    }

                    continue;
                }

                // ROTATION
                if (mode != EditMode::BUTTON_ACTION &&
                    mode != EditMode::BUTTON_YESNO &&
                    mode != EditMode::DIALOG_WINDOW)
                {
                    if (event.key.code == sf::Keyboard::Q)
                    {
                        activeCard.angle -= 90.f;
                        activeCard.sprite.setRotation(activeCard.angle);
                    }
                    if (event.key.code == sf::Keyboard::E)
                    {
                        activeCard.angle += 90.f;
                        activeCard.sprite.setRotation(activeCard.angle);
                    }
                }

                // FIXATION (F)
                if (event.key.code == sf::Keyboard::F)
                {
                    if (mode == EditMode::DIALOG_WINDOW)
                    {
                        if (!dialog.exists)
                        {
                            dialog.exists = true;
                            dialog.resizing = true;

                            dialog.x = activeMarker.getPosition().x;
                            dialog.y = activeMarker.getPosition().y;
                            dialog.w = 600.f;
                            dialog.h = 300.f;

                            uiText.setString(
                                L"Размер окна: 600 × 300   "
                                L"(←/→ ширина, ↑/↓ высота, Enter — подтвердить)");
                        }
                        continue;
                    }

                    if (mode == EditMode::BUTTON_ACTION)
                    {
                        ButtonSprite b;
                        b.id = "action";
                        b.sprite.setTexture(texAction);
                        b.sprite.setOrigin(texAction.getSize().x / 2.f, texAction.getSize().y / 2.f);
                        b.sprite.setPosition(activeMarker.getPosition());
                        layerButtons.buttons.push_back(b);

                        uiText.setString(L"Action button зафиксирована");
                        activeMarker.setPosition(1920.f / 2.f, 1080.f / 2.f);
                        continue;
                    }

                    if (mode == EditMode::BUTTON_YESNO)
                    {
                        sf::Vector2f pos = activeMarker.getPosition();

                        if (dialog.exists)
                        {
                            if (pos.x < dialog.x ||
                                pos.y < dialog.y ||
                                pos.x > dialog.x + dialog.w ||
                                pos.y > dialog.y + dialog.h)
                            {
                                uiText.setString(L"Кнопка должна быть внутри окна!");
                                continue;
                            }
                        }

                        if (waitingForYes)
                        {
                            ButtonSprite yes;
                            yes.id = "yes";
                            yes.sprite.setTexture(texYes);
                            yes.sprite.setOrigin(texYes.getSize().x / 2.f, texYes.getSize().y / 2.f);
                            yes.sprite.setPosition(pos);

                            layerButtons.buttons.push_back(yes);

                            waitingForYes = false;
                            waitingForNo = true;

                            uiText.setString(L"ДА зафиксирована. Теперь поставьте НЕТ.");
                        }
                        else if (waitingForNo)
                        {
                            sf::Vector2f yesPos;
                            bool foundYes = false;

                            for (auto &b : layerButtons.buttons)
                            {
                                if (b.id == "yes")
                                {
                                    yesPos = b.sprite.getPosition();
                                    foundYes = true;
                                    break;
                                }
                            }

                            if (!foundYes)
                            {
                                uiText.setString(L"Сначала поставьте ДА!");
                                continue;
                            }

                            pos.y = yesPos.y;

                            if (dialog.exists)
                            {
                                if (pos.x < dialog.x ||
                                    pos.y < dialog.y ||
                                    pos.x > dialog.x + dialog.w ||
                                    pos.y > dialog.y + dialog.h)
                                {
                                    uiText.setString(L"Кнопка НЕТ должна быть внутри окна!");
                                    continue;
                                }
                            }

                            ButtonSprite no;
                            no.id = "no";
                            no.sprite.setTexture(texNo);
                            no.sprite.setOrigin(texNo.getSize().x / 2.f, texNo.getSize().y / 2.f);
                            no.sprite.setPosition(pos);
                            layerButtons.buttons.push_back(no);

                            waitingForNo = false;

                            uiText.setString(L"ДА и НЕТ зафиксированы.");
                        }

                        continue;
                    }

                    if (mode == EditMode::DECK)
                    {
                        layerDeck.cards.clear();
                        layerDeck.cards.push_back(activeCard);

                        uiText.setString(L"Колода зафиксирована");
                        resetActiveCard();
                        continue;
                    }

                    if (mode == EditMode::TRUMP)
                    {
                        layerTrump.cards.clear();

                        CardSprite c = activeCard;
                        c.faceUp = true;
                        c.sprite.setTexture(texFaceDummy);

                        layerTrump.cards.push_back(c);

                        uiText.setString(L"Козырь зафиксирован");
                        resetActiveCard();
                        continue;
                    }

                    if (mode == EditMode::ATTACK_SLOTS)
                    {
                        if (pendingSlotStep == 0)
                        {
                            A1 = activeCard.sprite.getPosition();

                            layerAttack.cards.clear();
                            CardSprite c;
                            c.sprite.setTexture(texFaceDummy);
                            c.sprite.setOrigin(62.f, 90.f);
                            c.sprite.setPosition(A1);
                            layerAttack.cards.push_back(c);

                            pendingSlotStep = 1;
                            uiText.setString(L"Поставьте A2 и нажмите F");
                            resetActiveCard();
                        }
                        else if (pendingSlotStep == 1)
                        {
                            A2 = activeCard.sprite.getPosition();

                            float dx = A2.x - A1.x;
                            float dy = A2.y - A1.y;

                            layerAttack.cards.clear();
                            for (int i = 0; i < 6; i++)
                            {
                                CardSprite c;
                                c.sprite.setTexture(texFaceDummy);
                                c.sprite.setOrigin(62.f, 90.f);
                                c.sprite.setPosition(A1.x + dx * i, A1.y + dy * i);
                                layerAttack.cards.push_back(c);
                            }

                            pendingSlotStep = 2;
                            uiText.setString(L"Поставьте D1 и нажмите F");
                            resetActiveCard();
                        }
                        else if (pendingSlotStep == 2)
                        {
                            D1 = activeCard.sprite.getPosition();

                            sf::Vector2f offset = D1 - A1;
                            sf::Vector2f D2 = A2 + offset;

                            float dx = D2.x - D1.x;
                            float dy = D2.y - D1.y;

                            layerDefense.cards.clear();
                            for (int i = 0; i < 6; i++)
                            {
                                CardSprite c;
                                c.sprite.setTexture(texFaceDummy);
                                c.sprite.setOrigin(62.f, 90.f);
                                c.sprite.setPosition(D1.x + dx * i, D1.y + dy * i);
                                layerDefense.cards.push_back(c);
                            }

                            uiText.setString(L"Слоты атаки и защиты построены");
                            pendingSlotStep = 0;
                            resetActiveCard();
                        }

                        continue;
                    }
                } // F
            } // KEY PRESSED

            // MOUSE DRAGGING
            if (event.type == sf::Event::MouseButtonPressed &&
                event.mouseButton.button == sf::Mouse::Left)
            {
                sf::Vector2f m(event.mouseButton.x, event.mouseButton.y);

                if (mode == EditMode::BUTTON_ACTION ||
                    mode == EditMode::BUTTON_YESNO ||
                    mode == EditMode::DIALOG_WINDOW)
                {
                    if (activeMarker.getGlobalBounds().contains(m))
                    {
                        dragging = true;
                        dragOffset = activeMarker.getPosition() - m;
                    }
                }
                else
                {
                    if (activeCard.sprite.getGlobalBounds().contains(m))
                    {
                        dragging = true;
                        dragOffset = activeCard.sprite.getPosition() - m;
                    }
                }
            }

            if (event.type == sf::Event::MouseButtonReleased &&
                event.mouseButton.button == sf::Mouse::Left)
            {
                dragging = false;
            }

            if (event.type == sf::Event::MouseMoved && dragging)
            {
                sf::Vector2f m(event.mouseMove.x, event.mouseMove.y);

                if (mode == EditMode::BUTTON_ACTION ||
                    mode == EditMode::BUTTON_YESNO ||
                    mode == EditMode::DIALOG_WINDOW)
                {
                    activeMarker.setPosition(m + dragOffset);
                }
                else
                {
                    activeCard.sprite.setPosition(m + dragOffset);
                }
            }
        } // pollEvent

        // RENDER
        window.clear();
        window.draw(background);

        auto drawCards = [&](Layer &L)
        {
            for (auto &c : L.cards)
                window.draw(c.sprite);
        };

        auto drawButtons = [&](Layer &L)
        {
            for (auto &b : L.buttons)
                window.draw(b.sprite);
        };

        drawCards(layerTrump);
        drawCards(layerDeck);
        drawCards(layerAttack);
        drawCards(layerDefense);
        drawCards(layerBotHand);
        drawCards(layerPlrHand);

        if (dialog.exists)
        {
            sf::RectangleShape rect(sf::Vector2f(dialog.w, dialog.h));
            rect.setPosition(dialog.x, dialog.y);
            rect.setFillColor(sf::Color(0, 0, 0, 180));
            rect.setOutlineColor(sf::Color::White);
            rect.setOutlineThickness(3.f);
            window.draw(rect);

            sf::Text dialogText;
            dialogText.setFont(font);
            dialogText.setString(dialog.text);
            dialogText.setCharacterSize(36);
            dialogText.setFillColor(sf::Color::White);

            sf::FloatRect tb = dialogText.getLocalBounds();
            float tx = dialog.x + (dialog.w - tb.width) / 2.f - tb.left;
            float ty = dialog.y + 40.f;
            dialogText.setPosition(tx, ty);
            window.draw(dialogText);
        }

        drawButtons(layerButtons);

        if (mode == EditMode::BUTTON_ACTION ||
            mode == EditMode::BUTTON_YESNO ||
            mode == EditMode::DIALOG_WINDOW)
        {
            window.draw(activeMarker);
        }
        else if (mode != EditMode::VIEW_ALL)
        {
            window.draw(activeCard.sprite);
        }

        window.draw(uiText);
        window.display();
    }

    return 0;
}