#include "game.h"
#include <algorithm>
#include <cctype>
#include "game_passwords.cpp"

// Inicjalizacja
void game_init(Game &game) {
    game.secret = game_load_random_word("words.txt");
    game.guessed.clear();
    game.players.clear();
}

// Dodanie gracza
void game_add_player(Game &game, int fd) {
    Player p;
    p.nick = "";
    p.lives = 5;
    p.points = 0;
    game.players[fd] = p;
}

// Usunięcie gracza
void game_remove_player(Game &game, int fd) {
    game.players.erase(fd);
}

// Zakrycie hasła
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

// Obsługa wiadomości
std::string game_handle_message(Game &game, int fd, const std::string &msg) {

    // NICK
    if (msg.rfind("NICK ", 0) == 0) {
        std::string nick = msg.substr(5);

        for (auto &it : game.players) {
            if (it.second.nick == nick)
                return "NICK_TAKEN\n";
        }

        game.players[fd].nick = nick;
        return "NICK_OK\n";
    }

    // GUESS
    if (msg.rfind("GUESS ", 0) == 0) {
        char letter = toupper(msg[6]);

        if (game.guessed.count(letter))
            return "ALREADY_GUESSED\n";

        game.guessed.insert(letter);

        bool hit = false;
        for (char c : game.secret) {
            if (c == letter) {
                hit = true;
                game.players[fd].points += 10;
            }
        }

        if (!hit)
            game.players[fd].lives--;

        return "WORD " + masked_word(game) +
               " LIVES " + std::to_string(game.players[fd].lives) +
               " POINTS " + std::to_string(game.players[fd].points) + "\n";
    }

    return "";
}
