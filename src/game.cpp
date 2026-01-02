#include "game.h"
#include <algorithm>
#include <cctype>
#include "game_passwords.cpp"

// Init
void game_init(Game &game) {
    game.secret = game_load_random_word("words.txt");
    game.guessed.clear();

    for (auto &p : game.players) {
        p.second.lives = 5;
    }

    game.round_active = true;
    game.round++;
}

// Add a player
void game_add_player(Game &game, int fd) {
    Player p;
    p.nick = "";
    p.lives = 5;
    p.points = 0;
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

    for (auto &p : game.players) {
        p.second.lives = 5;
        p.second.points = 0;
    }
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
        return "NICK_OK\n";
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

           // Checking if anyone is alive
        if (!game_anyone_alive(game)) {

            game_start_new_round(game);

            return "ALL_LOST NEW_ROUND\n";
    }

    }


    if (game.players[fd].lives <= 0) {
        return "LOSE You are out of lives\n";
    }

    if (game.players[fd].lives <= 0) {

 
    return "YOU_LOST\n";
}

    std::string masked = masked_word(game);

    if (masked == game.secret) {
        game.round_active = false;

        std::string result =
            "WIN " + game.secret +
            " ROUND " + std::to_string(game.round) + "\n";

        // new round
        game_init(game);

        return result + "NEW_ROUND\n";
    }

    return "WORD " + masked_word(game) +
           " LIVES " + std::to_string(game.players[fd].lives) +
           " POINTS " + std::to_string(game.players[fd].points) + "\n";
}

    return "";
}

