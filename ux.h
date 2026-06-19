#pragma once
#include <SFML/Graphics.hpp>
#include <vector>
#include <string>

struct UxCommand
{
    std::string name;
    std::vector<std::string> args;
};

enum class ScResult { None, Resume, NewGame, Back, Exit };

void ux_init(sf::RenderWindow *win);
void ux_shutdown();
void ux_process_frame();
void ux_wait_all();

void ux_run_command(const UxCommand &cmd);

void ux_start_wait_player_move(const std::vector<std::string> &moves);
bool ux_move_ready();
int ux_get_chosen_move();

void ux_cmd(const std::string &name,
            const std::vector<std::string> &args = {});
void ux_wait_gameover_continue();

ScResult ux_wait_start_screen(bool canResume);
bool ux_is_menu_active();
void ux_reset_visuals();
