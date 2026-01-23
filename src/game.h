#ifndef GAME_H
#define GAME_H

#include <string>
#include <map>
#include <set>
#include <vector>
#include <ctime>

struct Player {
    std::string nick;
    int lives;
    int points;
    bool alive;
    int win_points;
    time_t last_activity;
    time_t nick_set_time = 0;
    bool guessed_warmup;
};

struct Game {
    std::string secret;
    std::set<char> guessed;
    std::set<char> wrong_letters;
    std::map<int, Player> players;

    bool round_active;
    bool game_started = false; 
    bool warmup_round = true;
    int warmup_guessed = 0;
    int round;
    int round_winner_fd;
    bool game_over_state = false;
    bool game_over_ranking_sent = false;
    static const int MAX_ROUNDS = 10;
    time_t game_over_time = 0;
    time_t round_start_time = 0;
    time_t warmup_start_time = 0;
    static const int ROUND_TIME_LIMIT = 180;

    std::string last_secret;

    // Pomocnicze zmienne do śledzenia liczników (zamiast static w main)
    int last_warmup_val = -1;
    int last_inter_val = -1;
    int last_round_val = -1;
    int last_gameover_val = -1;
};

void game_init(Game &game);
void game_add_player(Game &game, int fd);
void game_remove_player(Game &game, int fd);
std::string game_handle_message(Game &game, int fd, const std::string &msg);
std::string get_wrong_letters(const Game &game);
void award_win_points(Game &game);
std::string get_rankings(const Game &game);
std::string get_final_rankings(const Game &game);
std::string masked_word(const Game &game);
std::string get_all_players_stats(const Game &game);
bool game_anyone_alive(const Game &game);

#endif