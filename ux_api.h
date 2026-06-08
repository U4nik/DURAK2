#pragma once
#include <string>
#include <vector>
#include <SFML/Graphics.hpp>

void ux_init(sf::RenderWindow* win);
void ux_shutdown();

// отправка UX-команды из движка
void ux_run_command(const std::string& name,
                    const std::vector<std::string>& args);

// главный кадр UX
void ux_process_frame();

// ожидание завершения всех анимаций
void ux_wait_all();

// API для выбора хода игроком
void ux_start_wait_player_move(const std::vector<std::string>& moves);
bool ux_move_ready();
int  ux_get_chosen_move();