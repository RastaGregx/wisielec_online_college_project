#ifndef GAME_H
#define GAME_H

#include <string>
#include <map>
#include <set>

struct Player {
    std::string nick;
    int lives;
    int points;
    bool alive;
};

// Dane gracza
struct Game {
    std::string secret;
    std::set<char> guessed;
    std::map<int, Player> players;

    bool round_active;
    int round;
};

// Inicjalizacja gry
void game_init(Game &game);

// Dodanie / usunięcie gracza
void game_add_player(Game &game, int fd);
void game_remove_player(Game &game, int fd);

// Obsługa wiadomości od klienta
// Zwraca string do rozesłania (default = "")
std::string game_handle_message(Game &game, int fd, const std::string &msg);
void game_start_new_round(Game &game);
#endif