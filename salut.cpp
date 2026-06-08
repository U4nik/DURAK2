#include "salut.h"
#include <cmath>

// ------------------------------------------------------------
// COLOR FUNCTIONS
// ------------------------------------------------------------

static sf::Color getRedShade(float variation) {
    int r = 150 + static_cast<int>(105 * variation);
    int g = 20 + static_cast<int>(40 * variation);
    int b = 20 + static_cast<int>(40 * variation);
    return sf::Color(r, g, b);
}

static sf::Color getGreenShade(float variation) {
    int r = 20 + static_cast<int>(100 * variation);
    int g = 100 + static_cast<int>(155 * variation);
    int b = 20 + static_cast<int>(60 * variation);
    return sf::Color(r, g, b);
}

static sf::Color getBlueShade(float variation) {
    int r = 20 + static_cast<int>(40 * variation);
    int g = 50 + static_cast<int>(150 * variation);
    int b = 150 + static_cast<int>(105 * variation);
    return sf::Color(r, g, b);
}

static sf::Color getPurpleShade(float variation) {
    int r = 120 + static_cast<int>(135 * variation);
    int g = 40 + static_cast<int>(100 * variation);
    int b = 150 + static_cast<int>(105 * variation);
    return sf::Color(r, g, b);
}

static sf::Color getYellowShade(float variation) {
    int r = 180 + static_cast<int>(75 * variation);
    int g = 140 + static_cast<int>(115 * variation);
    int b = 20 + static_cast<int>(100 * variation);
    return sf::Color(r, g, b);
}

sf::Color FireworksManager::getShadeFromFamily(int family, float variation) {
    switch(family) {
        case 0: return getRedShade(variation);
        case 1: return getGreenShade(variation);
        case 2: return getBlueShade(variation);
        case 3: return getPurpleShade(variation);
        case 4: return getYellowShade(variation);
        default: return sf::Color::White;
    }
}

// ------------------------------------------------------------
// FIREWORKS MANAGER IMPLEMENTATION
// ------------------------------------------------------------

FireworksManager::FireworksManager(float windowWidth, float windowHeight)
    : m_windowWidth(windowWidth)
    , m_windowHeight(windowHeight)
    , m_timeSinceLastLaunch(0.0f)
    , m_launchInterval(0.8f)
    , m_active(false)
{
    std::srand(static_cast<unsigned>(std::time(nullptr)));
}

void FireworksManager::start() {
    m_active = true;
    m_timeSinceLastLaunch = 0.0f;
}

void FireworksManager::stop() {
    m_active = false;
    m_rockets.clear();
    m_debris.clear();
}

bool FireworksManager::isActive() const {
    return m_active;
}

void FireworksManager::launchRocket() {
    Rocket r;
    
    float startX = 100 + rand() % static_cast<int>(m_windowWidth - 200);
    r.shape.setRadius(3 + rand() % 4);
    
    r.colorFamily = rand() % 5;
    
    sf::Color startColor = getShadeFromFamily(r.colorFamily, 0.5f);
    startColor.a = 40;
    r.shape.setFillColor(startColor);
    
    r.shape.setPosition(startX, m_windowHeight - 50);
    
    float speed = 200 + rand() % 350;
    r.velocity = sf::Vector2f(0, -speed);
    
    r.targetY = 100 + rand() % static_cast<int>(m_windowHeight * 0.7);
    r.brightness = 0.1f + (rand() % 100) / 200.0f;
    
    r.maxTrailLength = 10 + rand() % 20;
    
    m_rockets.push_back(r);
}

void FireworksManager::update(float dt) {
    if (!m_active) return;
    
    m_timeSinceLastLaunch += dt;
    
    // Автоматический запуск ракет
    if (m_timeSinceLastLaunch >= m_launchInterval) {
        m_timeSinceLastLaunch = 0.0f;
        
        int numRockets = 1 + rand() % 3; // 1-3 ракеты
        for (int i = 0; i < numRockets; ++i) {
            launchRocket();
        }
    }
    
    // --- Обновление ракет ---
    for (auto it = m_rockets.begin(); it != m_rockets.end(); ) {
        // Шлейф
        if (it->trail.size() > 0 || rand() % 3 == 0) {
            sf::CircleShape trailPart = it->shape;
            sf::Color trailColor = trailPart.getFillColor();
            trailColor.a = 80;
            trailPart.setFillColor(trailColor);
            trailPart.setRadius(trailPart.getRadius() * 0.7f);
            
            it->trail.push_front(trailPart);
            if (it->trail.size() > it->maxTrailLength) {
                it->trail.pop_back();
            }
        }
        
        // Движение
        it->shape.move(it->velocity * dt);
        
        // Яркость
        it->brightness += 1.2f * dt;
        if (it->brightness > 1.0f) it->brightness = 1.0f;
        
        // Цвет
        sf::Color rocketColor = getShadeFromFamily(it->colorFamily, 0.5f);
        rocketColor.a = static_cast<sf::Uint8>(it->brightness * 255);
        it->shape.setFillColor(rocketColor);
        
        // Шлейф
        float alphaStep = 80.0f / it->trail.size();
        int index = 0;
        for (auto& trailPart : it->trail) {
            sf::Color color = trailPart.getFillColor();
            color.a = static_cast<sf::Uint8>(80 - index * alphaStep);
            if (color.a < 10) color.a = 10;
            trailPart.setFillColor(color);
            index++;
        }
        
        // Взрыв
        if (it->shape.getPosition().y <= it->targetY) {
            sf::Vector2f pos = it->shape.getPosition();
            int numDebris = 60 + rand() % 80;
            
            for (int i = 0; i < numDebris; ++i) {
                Debris d;
                d.shape.setRadius(2 + rand() % 7);
                d.shape.setPosition(pos);
                
                float variation = static_cast<float>(rand() % 100) / 100.0f;
                if (rand() % 10 < 4) {
                    variation = (rand() % 2 == 0) ? 0.1f : 0.95f;
                }
                
                sf::Color debrisColor = getShadeFromFamily(it->colorFamily, variation);
                d.shape.setFillColor(debrisColor);
                
                float angle = (rand() % 360) * 3.14159f / 180.f;
                float speed = 80 + rand() % 350;
                d.velocity = sf::Vector2f(std::cos(angle) * speed, std::sin(angle) * speed);
                
                d.lifetime = 1.2f + (rand() % 250) / 100.f;
                d.maxLifetime = d.lifetime;
                d.maxTrailLength = 5 + rand() % 10;
                
                m_debris.push_back(d);
            }
            it = m_rockets.erase(it);
        } else {
            ++it;
        }
    }
    
    // --- Обновление осколков ---
    for (auto it = m_debris.begin(); it != m_debris.end(); ) {
        // Шлейф
        sf::CircleShape trailPart = it->shape;
        sf::Color trailColor = trailPart.getFillColor();
        trailColor.a = 60;
        trailPart.setFillColor(trailColor);
        trailPart.setRadius(trailPart.getRadius() * 0.6f);
        
        it->trail.push_front(trailPart);
        if (it->trail.size() > it->maxTrailLength) {
            it->trail.pop_back();
        }
        
        // Физика
        it->velocity.y += 350 * dt;
        it->shape.move(it->velocity * dt);
        
        // Время жизни
        it->lifetime -= dt;
        
        // Прозрачность
        float alpha = (it->lifetime / it->maxLifetime) * 255;
        if (alpha < 0) alpha = 0;
        sf::Color color = it->shape.getFillColor();
        color.a = static_cast<sf::Uint8>(alpha);
        it->shape.setFillColor(color);
        
        // Шлейф
        float trailAlphaStep = 60.0f / it->trail.size();
        int index = 0;
        for (auto& trailPart : it->trail) {
            sf::Color trailColor = trailPart.getFillColor();
            trailColor.a = static_cast<sf::Uint8>((alpha * 0.3f) - index * trailAlphaStep);
            if (trailColor.a < 5) trailColor.a = 5;
            trailPart.setFillColor(trailColor);
            index++;
        }
        
        // Удаление
        if (it->lifetime <= 0.0f) {
            it = m_debris.erase(it);
        } else {
            ++it;
        }
    }
}

void FireworksManager::draw(sf::RenderWindow& window) {
    if (!m_active) return;
    
    // Шлейфы
    for (const auto& r : m_rockets) {
        for (const auto& trailPart : r.trail) {
            window.draw(trailPart);
        }
    }
    for (const auto& d : m_debris) {
        for (const auto& trailPart : d.trail) {
            window.draw(trailPart);
        }
    }
    
    // Частицы
    for (const auto& r : m_rockets)
        window.draw(r.shape);
    for (const auto& d : m_debris)
        window.draw(d.shape);
}
