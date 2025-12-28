#ifndef GAME_H
#define GAME_H

#include <string>
#include <map>
#include <set>

// Dane gracza
struct Player {
    std::string nick;
    int lives;
    int points;
};

// Stan gry
struct Game {
    std::string secret;
    std::set<char> guessed;
    std::map<int, Player> players; // fd -> Player
};

// Inicjalizacja gry
void game_init(Game &game);

// Dodanie / usunięcie gracza
void game_add_player(Game &game, int fd);
void game_remove_player(Game &game, int fd);

// Obsługa wiadomości od klienta
// Zwraca string do rozesłania (default = "")
std::string game_handle_message(Game &game, int fd, const std::string &msg);

#endif