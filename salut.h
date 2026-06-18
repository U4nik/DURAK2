#pragma once

#include <SFML/Graphics.hpp>
#include <vector>
#include <deque>
#include <functional>
#include <cstdlib>
#include <ctime>

// ------------------------------------------------------------
// FIREWORKS PARTICLES
// ------------------------------------------------------------

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

// ------------------------------------------------------------
// FIREWORKS MANAGER
// ------------------------------------------------------------

class FireworksManager {
public:
    FireworksManager(float windowWidth, float windowHeight);
    
    // Запуск салюта
    void start();
    
    // Остановка и очистка
    void stop();
    
    // Обновление физики
    void update(float dt);
    
    // Отрисовка
    void draw(sf::RenderWindow& window);
    
    // Проверка: активен ли салют
    bool isActive() const;

    // Установка колбэков для звуков
    void setOnRocketLaunch(std::function<void()> callback);
    void setOnExplosion(std::function<void()> callback);

private:
    // Запуск одной ракеты
    void launchRocket();
    
    // Получение цвета
    sf::Color getShadeFromFamily(int family, float variation);
    
    std::function<void()> m_onRocketLaunch;
    std::function<void()> m_onExplosion;

    float m_windowWidth;
    float m_windowHeight;
    
    std::vector<Rocket> m_rockets;
    std::vector<Debris> m_debris;
    
    float m_timeSinceLastLaunch;
    float m_launchInterval;
    
    bool m_active;
};
