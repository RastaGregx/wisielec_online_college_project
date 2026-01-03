#include "game.h"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <vector>
#include "game_passwords.cpp"


// Init
void game_init(Game &game) {
    game.secret = game_load_random_word("words.txt");
    game.guessed.clear();
    game.wrong_letters.clear();

    for (auto &p : game.players) {
        p.second.lives = 5;
        p.second.points = 0;
    }

    game.round_active = true;
    game.round++;
    game.round_winner_fd = -1;
}

// Add a player
void game_add_player(Game &game, int fd) {
    Player p;
    p.nick = "";
    p.lives = 5;
    p.points = 0;
    p.win_points = 0;
    game.players[fd] = p;
}

// Remove a player
void game_remove_player(Game &game, int fd) {
    game.players.erase(fd);
}

// Hide the word
static std::string masked_word(const Game &game) {
    std::string out;
    for (char c : game.secret) {
        if (game.guessed.count(c))
            out += c;
        else
            out += '_';
    }
    return out;
}

bool game_anyone_alive(const Game &game) {
    for (const auto &p : game.players) {
        if (p.second.lives > 0)
            return true;
    }
    return false;
}

void game_start_new_round(Game &game) {
    game.secret = game_load_random_word("words.txt");
    game.guessed.clear();
    game.wrong_letters.clear();

    for (auto &p : game.players) {
        p.second.lives = 5;
        p.second.points = 0;
    }
    
    game.round_active = true;
    game.round_winner_fd = -1;
}

// Get wrong letters as a string
std::string get_wrong_letters(const Game &game) {
    std::string wrong;
    for (char c : game.wrong_letters) {
        wrong += c;
        wrong += " ";
    }
    if (!wrong.empty() && wrong.back() == ' ') {
        wrong.pop_back();
    }
    return wrong;
}

std::vector<int> get_top3_fds(const Game &game) {
    std::vector<std::pair<int, int>> scores;
    
    for (const auto &p : game.players) {
        scores.push_back({p.first, p.second.points});
    }
    
    // Sort descending by points
    std::sort(scores.begin(), scores.end(), 
        [](const std::pair<int,int> &a, const std::pair<int,int> &b) {
            return a.second > b.second;
        });
    
    std::vector<int> top3;
    for (size_t i = 0; i < std::min(size_t(3), scores.size()); i++) {
        top3.push_back(scores[i].first);
    }
    
    return top3;
}

void award_win_points(Game &game) {
    std::vector<int> top3 = get_top3_fds(game);
    
    // 1. place = 3 pts, 2. palce = 2 pts, 3. place = 1 pts
    int points_for_place[] = {3, 2, 1};
    
    for (size_t i = 0; i < top3.size(); i++) {
        int fd = top3[i];
        game.players[fd].win_points += points_for_place[i];
    }
    
    // Bonus +2 pts for player who guessed the word
    if (game.round_winner_fd != -1 && game.players.count(game.round_winner_fd)) {
        game.players[game.round_winner_fd].win_points += 2;
    }
}

std::string get_rankings(const Game &game) {
    std::vector<std::pair<int, int>> scores; // pair<fd, points>
    
    for (const auto &p : game.players) {
        scores.push_back({p.first, p.second.points});
    }
    
    std::sort(scores.begin(), scores.end(), 
        [](const std::pair<int,int> &a, const std::pair<int,int> &b) {
            return a.second > b.second;
        });
    
    std::stringstream ss;
    ss << "ROUND " << game.round << " RANKINGS:\n";
    
    for (size_t i = 0; i < scores.size(); i++) {
        int fd = scores[i].first;
        const Player &p = game.players.at(fd);
        
        ss << (i + 1) << ". " << p.nick << " - " << p.points << " pts";
        
        if (i < 3) ss << " [TOP3]";
        if (fd == game.round_winner_fd) ss << " [WINNER]";
        
        ss << "\n";
    }
    
    return ss.str();
}

std::string get_all_players_stats(const Game &game) {
    std::string stats = "PLAYERS_LIST ";
    
    for (const auto &p : game.players) {
        stats += p.second.nick + ":" + 
                 std::to_string(p.second.lives) + ":" + 
                 std::to_string(p.second.points) + ":" + 
                 std::to_string(p.second.win_points) + ",";
    }
    
    // Remove last comma ","
    if (!stats.empty() && stats.back() == ',') {
        stats.pop_back();
    }
    
    return stats + "\n";
}

// Message handling
std::string game_handle_message(Game &game, int fd, const std::string &msg) {

    // NICK
    if (msg.rfind("NICK ", 0) == 0) {
        std::string nick = msg.substr(5);

        // Check if the nick is taken
        for (auto &p : game.players) {
            if (p.second.nick == nick) {
                return "NICK_TAKEN\n";
            }
        }

        // Set the nick if it's ok
        game.players[fd].nick = nick;

        std::string masked = masked_word(game);
        std::string wrong = get_wrong_letters(game);

        std::string response = "NICK_OK\n";
        response += "BROADCAST WORD " + masked + " WRONG " + wrong + "\n";
        response += "BROADCAST " + get_all_players_stats(game);
        response += "PERSONAL LIVES " + std::to_string(game.players[fd].lives);
        response += " POINTS " + std::to_string(game.players[fd].points);
        response += " WINPTS " + std::to_string(game.players[fd].win_points) + "\n";
        return response;
    }

    // GUESS
    if (msg.rfind("GUESS ", 0) == 0) {
        std::string guess = msg.substr(6);
        guess.erase(guess.find_last_not_of("\r\n") + 1);

        // Round end
        if (!game.round_active) {
            return "WAIT New round starting...\n";
        }

        // Player eliminated
        if (game.players[fd].lives <= 0) {
            return "DEAD You have no lives left\n";
        }

        // Exactly one letter
        if (guess.size() != 1) {
            return "ERROR You must guess exactly one letter\n";
        }

        char letter = toupper(guess[0]);

        // Only letter allowed
        if (!isalpha(letter)) {
            return "ERROR Guess must be a letter\n";
        }

        // Already guessed letter
        if (game.guessed.count(letter)) {
            return "ALREADY_GUESSED\n";
        }

        game.guessed.insert(letter);

        bool hit = false;
        for (char c : game.secret) {
            if (c == letter) {
                hit = true;
                game.players[fd].points += 10;
            }
        }

        if (!hit) {
            game.players[fd].lives--;
            game.wrong_letters.insert(letter);

            // Checking if anyone is alive
            if (!game_anyone_alive(game)) {
                award_win_points(game);
                std::string rankings = get_rankings(game);

                game_start_new_round(game);
                
                // Send stats to everyone
                std::string masked = masked_word(game);
                std::string wrong = get_wrong_letters(game);
                
                std::string response = "BROADCAST ALL_LOST NEW_ROUND\n";
                response += "BROADCAST WORD " + masked + " WRONG " + wrong + "\n";
                response += "BROADCAST " + get_all_players_stats(game);

                for (const auto &p : game.players) {
                    response += "PERSONAL_TO_" + std::to_string(p.first) + " LIVES " + 
                    std::to_string(p.second.lives) + " POINTS " + 
                    std::to_string(p.second.points) + " WINPTS " + 
                    std::to_string(p.second.win_points) + "\n";
                }
                return response;
            }
        }

        
        if (game.players[fd].lives <= 0) {
            std::string masked = masked_word(game);
            std::string wrong = get_wrong_letters(game);
            
            std::string response = "BROADCAST WORD " + masked + " WRONG " + wrong + "\n";
            response += "BROADCAST " + get_all_players_stats(game);
            response += "PERSONAL LIVES 0 POINTS " + std::to_string(game.players[fd].points) + 
                        " WINPTS " + std::to_string(game.players[fd].win_points) + "\n";
            response += "YOU_LOST\n";
            
            return response;
        }
        
        std::string masked = masked_word(game);

        if (masked == game.secret) {
            game.round_active = false;
            game.round_winner_fd = fd;

            award_win_points(game);
            std::string rankings = get_rankings(game);

            std::string result =
                "BROADCAST WIN " + game.secret +
                " ROUND " + std::to_string(game.round) + "\n";

            result += "BROADCAST " + rankings;

            // new round
            game_init(game);

            std::string masked_new = masked_word(game);
            std::string wrong_new = get_wrong_letters(game);

            result += "BROADCAST NEW_ROUND\n";
            result += "BROADCAST WORD " + masked_new + " WRONG " + wrong_new + "\n";
            result += "BROADCAST " + get_all_players_stats(game);  

            for (const auto &p : game.players) {
                result += "PERSONAL_TO_" + std::to_string(p.first) + " LIVES " + 
                std::to_string(p.second.lives) + " POINTS " + 
                std::to_string(p.second.points) + " WINPTS " + 
                std::to_string(p.second.win_points) + "\n";
            }

            return result;
        }

        // Broadcast to all players
        std::string wrong = get_wrong_letters(game);
        std::string broadcast = "BROADCAST WORD " + masked + " WRONG " + wrong + "\n";
        broadcast += "BROADCAST " + get_all_players_stats(game);


        std::string personal = "PERSONAL LIVES " + std::to_string(game.players[fd].lives) +
            " POINTS " + std::to_string(game.players[fd].points) + 
            " WINPTS " + std::to_string(game.players[fd].win_points) + "\n";


        return broadcast + personal;
    }

    return "";
}