#include <SFML/Graphics.hpp>
#include <vector>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <deque>

struct Rocket {
    sf::CircleShape shape;
    sf::Vector2f velocity;
    float targetY;
    float brightness;
    int colorFamily;          // 0-4: красный, зеленый, синий, фиолетовый, желтый
    
    // Для шлейфа ракеты
    std::deque<sf::CircleShape> trail;
    int maxTrailLength = 20;  // длина шлейфа
};

struct Debris {
    sf::CircleShape shape;
    sf::Vector2f velocity;
    float lifetime;
    float maxLifetime;
    
    // Для шлейфа осколков
    std::deque<sf::CircleShape> trail;
    int maxTrailLength = 10;  // покороче для осколков
};

// Функция для получения случайного цветового семейства (0-4)
int getRandomColorFamily() {
    return rand() % 5;
}

// Функция для получения оттенка красного
sf::Color getRedShade(float variation) {
    int r = 150 + static_cast<int>(105 * variation);
    int g = 20 + static_cast<int>(40 * variation);
    int b = 20 + static_cast<int>(40 * variation);
    return sf::Color(r, g, b);
}

// Функция для получения оттенка зеленого
sf::Color getGreenShade(float variation) {
    int r = 20 + static_cast<int>(100 * variation);
    int g = 100 + static_cast<int>(155 * variation);
    int b = 20 + static_cast<int>(60 * variation);
    return sf::Color(r, g, b);
}

// Функция для получения оттенка синего
sf::Color getBlueShade(float variation) {
    int r = 20 + static_cast<int>(40 * variation);
    int g = 50 + static_cast<int>(150 * variation);
    int b = 150 + static_cast<int>(105 * variation);
    return sf::Color(r, g, b);
}

// Функция для получения оттенка фиолетового
sf::Color getPurpleShade(float variation) {
    int r = 120 + static_cast<int>(135 * variation);
    int g = 40 + static_cast<int>(100 * variation);
    int b = 150 + static_cast<int>(105 * variation);
    return sf::Color(r, g, b);
}

// Функция для получения оттенка желтого
sf::Color getYellowShade(float variation) {
    int r = 180 + static_cast<int>(75 * variation);
    int g = 140 + static_cast<int>(115 * variation);
    int b = 20 + static_cast<int>(100 * variation);
    return sf::Color(r, g, b);
}

// Основная функция получения оттенка по семейству и вариации
sf::Color getShadeFromFamily(int family, float variation) {
    switch(family) {
        case 0: return getRedShade(variation);
        case 1: return getGreenShade(variation);
        case 2: return getBlueShade(variation);
        case 3: return getPurpleShade(variation);
        case 4: return getYellowShade(variation);
        default: return sf::Color::White;
    }
}

int main() {
    std::srand(static_cast<unsigned>(std::time(nullptr)));

    sf::VideoMode desktopMode = sf::VideoMode::getDesktopMode();
    sf::RenderWindow window(desktopMode, "Fireworks with Trails!", sf::Style::Fullscreen);
    window.setFramerateLimit(60);

    float windowWidth = static_cast<float>(window.getSize().x);
    float windowHeight = static_cast<float>(window.getSize().y);

    sf::Clock clock;

    std::vector<Rocket> rockets;
    std::vector<Debris> debris;

    float timeSinceLastLaunch = 0.0f;
    float launchInterval = 1.2f; // чуть чаще для красоты

    while (window.isOpen()) {
        float dt = clock.restart().asSeconds();
        timeSinceLastLaunch += dt;

        sf::Event event;
        while (window.pollEvent(event)) {
            if (event.type == sf::Event::Closed)
                window.close();
            if (event.type == sf::Event::KeyPressed) {
                if (event.key.code == sf::Keyboard::Escape) {
                    window.close();
                }
                if (event.key.code == sf::Keyboard::Space) {
                    // Ручной запуск
                    Rocket r;
                    
                    float startX = 100 + rand() % static_cast<int>(windowWidth - 200);
                    r.shape.setRadius(5); // чуть крупнее
                    
                    r.colorFamily = getRandomColorFamily();
                    
                    sf::Color startColor = getShadeFromFamily(r.colorFamily, 0.5f);
                    startColor.a = 50;
                    r.shape.setFillColor(startColor);
                    
                    r.shape.setPosition(startX, windowHeight - 50);
                    
                    float speed = 300 + rand() % 250;
                    r.velocity = sf::Vector2f(0, -speed);
                    
                    r.targetY = 150 + rand() % 600;
                    r.brightness = 0.2f;
                    
                    r.maxTrailLength = 15 + rand() % 15; // разнообразие длины хвостов
                    
                    rockets.push_back(r);
                }
            }
        }

        // Автоматический запуск
        if (timeSinceLastLaunch >= launchInterval) {
            timeSinceLastLaunch = 0.0f;
            
            int numRockets = 1 + rand() % 4; // до 4 ракет одновременно
            for (int i = 0; i < numRockets; ++i) {
                Rocket r;
                float startX = 100 + rand() % static_cast<int>(windowWidth - 200);
                r.shape.setRadius(3 + rand() % 4);
                
                r.colorFamily = getRandomColorFamily();
                
                sf::Color startColor = getShadeFromFamily(r.colorFamily, 0.5f);
                startColor.a = 40;
                r.shape.setFillColor(startColor);
                
                r.shape.setPosition(startX, windowHeight - 50);
                
                float speed = 200 + rand() % 350;
                r.velocity = sf::Vector2f(0, -speed);
                
                r.targetY = 100 + rand() % static_cast<int>(windowHeight * 0.7);
                r.brightness = 0.1f + (rand() % 100) / 200.0f;
                
                r.maxTrailLength = 10 + rand() % 20;
                
                rockets.push_back(r);
            }
        }

        // --- Обновление ракет ---
        for (auto it = rockets.begin(); it != rockets.end(); ) {
            // Сохраняем текущую позицию для шлейфа ПЕРЕД движением
            if (it->trail.size() > 0 || rand() % 3 == 0) { // не каждый кадр, для вариативности
                sf::CircleShape trailPart = it->shape;
                sf::Color trailColor = trailPart.getFillColor();
                trailColor.a = 80; // полупрозрачный шлейф
                trailPart.setFillColor(trailColor);
                trailPart.setRadius(trailPart.getRadius() * 0.7f); // чуть меньше
                
                it->trail.push_front(trailPart);
                if (it->trail.size() > it->maxTrailLength) {
                    it->trail.pop_back();
                }
            }
            
            // Двигаем ракету
            it->shape.move(it->velocity * dt);
            
            // Увеличиваем яркость
            it->brightness += 1.2f * dt;
            if (it->brightness > 1.0f) it->brightness = 1.0f;

            // Обновляем цвет ракеты
            sf::Color rocketColor = getShadeFromFamily(it->colorFamily, 0.5f);
            rocketColor.a = static_cast<sf::Uint8>(it->brightness * 255);
            it->shape.setFillColor(rocketColor);

            // Обновляем цвета в шлейфе (делаем их более прозрачными со временем)
            float alphaStep = 80.0f / it->trail.size();
            int index = 0;
            for (auto& trailPart : it->trail) {
                sf::Color color = trailPart.getFillColor();
                color.a = static_cast<sf::Uint8>(80 - index * alphaStep);
                if (color.a < 10) color.a = 10;
                trailPart.setFillColor(color);
                index++;
            }

            // Проверка взрыва
            if (it->shape.getPosition().y <= it->targetY) {
                // Взрыв - создаем осколки
                sf::Vector2f pos = it->shape.getPosition();
                int numDebris = 60 + rand() % 80; // больше осколков
                
                for (int i = 0; i < numDebris; ++i) {
                    Debris d;
                    d.shape.setRadius(2 + rand() % 7);
                    d.shape.setPosition(pos);
                    
                    float variation = static_cast<float>(rand() % 100) / 100.0f;
                    if (rand() % 10 < 4) { // 40% экстремальных оттенков
                        variation = (rand() % 2 == 0) ? 0.1f : 0.95f;
                    }
                    
                    sf::Color debrisColor = getShadeFromFamily(it->colorFamily, variation);
                    d.shape.setFillColor(debrisColor);

                    float angle = (rand() % 360) * 3.14159f / 180.f;
                    float speed = 80 + rand() % 350;
                    d.velocity = sf::Vector2f(std::cos(angle) * speed, std::sin(angle) * speed);

                    d.lifetime = 1.2f + (rand() % 250) / 100.f; // 1.2-3.7 секунды
                    d.maxLifetime = d.lifetime;
                    d.maxTrailLength = 5 + rand() % 10;
                    
                    debris.push_back(d);
                }
                it = rockets.erase(it);
            } else {
                ++it;
            }
        }

        // --- Обновление осколков ---
        for (auto it = debris.begin(); it != debris.end(); ) {
            // Сохраняем для шлейфа (каждый кадр для густоты)
            sf::CircleShape trailPart = it->shape;
            sf::Color trailColor = trailPart.getFillColor();
            trailColor.a = 60; // более прозрачный шлейф
            trailPart.setFillColor(trailColor);
            trailPart.setRadius(trailPart.getRadius() * 0.6f);
            
            it->trail.push_front(trailPart);
            if (it->trail.size() > it->maxTrailLength) {
                it->trail.pop_back();
            }
            
            // Физика
            it->velocity.y += 350 * dt; // гравитация
            it->shape.move(it->velocity * dt);

            // Уменьшаем время жизни
            it->lifetime -= dt;

            // Обновляем прозрачность основной частицы
            float alpha = (it->lifetime / it->maxLifetime) * 255;
            if (alpha < 0) alpha = 0;
            sf::Color color = it->shape.getFillColor();
            color.a = static_cast<sf::Uint8>(alpha);
            it->shape.setFillColor(color);

            // Обновляем прозрачность шлейфа
            float trailAlphaStep = 60.0f / it->trail.size();
            int index = 0;
            for (auto& trailPart : it->trail) {
                sf::Color trailColor = trailPart.getFillColor();
                trailColor.a = static_cast<sf::Uint8>((alpha * 0.3f) - index * trailAlphaStep);
                if (trailColor.a < 5) trailColor.a = 5;
                trailPart.setFillColor(trailColor);
                index++;
            }

            // Удаляем мёртвые частицы
            if (it->lifetime <= 0.0f) {
                it = debris.erase(it);
            } else {
                ++it;
            }
        }

        // --- Отрисовка ---
        window.clear(sf::Color(5, 5, 15)); // почти черный с синевой

        // Сначала рисуем все шлейфы (они должны быть под основными частицами)
        for (const auto& r : rockets) {
            for (const auto& trailPart : r.trail) {
                window.draw(trailPart);
            }
        }
        for (const auto& d : debris) {
            for (const auto& trailPart : d.trail) {
                window.draw(trailPart);
            }
        }
        
        // Потом сами частицы
        for (const auto& r : rockets)
            window.draw(r.shape);
        for (const auto& d : debris)
            window.draw(d.shape);

        window.display();
    }

    return 0;
}