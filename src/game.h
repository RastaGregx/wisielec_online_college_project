#ifndef GAME_H
#define GAME_H

#include <string>
#include <map>
#include <set>
#include <vector>

// Player data
struct Player {
    std::string nick;
    int lives;
    int points;
    bool alive;
    int win_points;
};

//Game data
struct Game {
    std::string secret;
    std::set<char> guessed;
    std::set<char> wrong_letters;
    std::map<int, Player> players;

    bool round_active;
    int round;
    int round_winner_fd;
};

// Init
void game_init(Game &game);

// Add/remove player
void game_add_player(Game &game, int fd);
void game_remove_player(Game &game, int fd);


// Message handling from player
// Returns string to send (default = "") 
std::string game_handle_message(Game &game, int fd, const std::string &msg);
void game_start_new_round(Game &game);
std::string get_wrong_letters(const Game &game);

std::vector<int> get_top3_fds(const Game &game);
void award_win_points(Game &game);
std::string get_rankings(const Game &game);

std::string get_all_players_stats(const Game &game);

#endif