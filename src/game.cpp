#include "game.h"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <vector>
#include "game_passwords.cpp" 

//=========================================
// Zamiana tekstu w format dla rozdzielacza
//=========================================
std::string broadcast_multiline(const std::string &text) {
    std::stringstream ss(text);
    std::string line;
    std::string out;
    while (std::getline(ss, line)) {
        if (!line.empty()) out += "BROADCAST " + line + "\n";
    }
    return out;
}

//=========================================
// Losowanie słowa, reset statystyk, czy gra może się zacząć
//=========================================
void game_init(Game &game) {
    game.guessed.clear();                   // Usuwa odgadnięte litery
    game.wrong_letters.clear();             // Czyści listę błędnych liter
    game.round_start_time = time(nullptr);  // Zapisanie aktualnej sekundy jako czas startu
    game.round_winner_fd = -1;              // Resetuje ID zwycięzcy
    game.round_active = true;               // Pozwala serwerowi przyjmować litery od graczy
    game.game_started = true;               // Informacja, że gra się zaczęła i nie jesteśmy w lobby

    // Liczenie graczy z nickiem
    int players_with_nick = 0;
    for (const auto &p : game.players) {
        if (!p.second.nick.empty()) players_with_nick++;
    }

    // Pilnuje czy jest co najmniej 2 graczy
    if (players_with_nick < 2) {
        game.round_active = false;
        game.game_started = false;
        return;
    }

    game.round_active = true;
    game.game_started = true;

    // Resetowanie statystyk każdego z graczy
    for (auto &p : game.players) {
        if (p.second.nick.empty()) {
            // Gracz widmo (bez nicku) nie bierze udziału
            p.second.lives = 0;
            p.second.alive = false;
            p.second.points = 0;
        } else {
            p.second.lives = 5;
            p.second.points = 0;
            p.second.alive = true;
            p.second.guessed_warmup = false;
        }
    }

    // Wybór hasła
    if (game.warmup_round) {
        game.secret = "R";
    } else {
        // Spróbuj wylosować inne hasło niż poprzednie (max prób = 10).
        std::string new_word;
        const int MAX_ATTEMPTS = 10;
        int attempts = 0;
        do {
            new_word = game_load_random_word("words.txt");
            attempts++;
            // Jeśli plik ma tylko jedno słowo, petla zakończy się po MAX_ATTEMPTS i zaakceptuje to samo słowo.
        } while (!game.last_secret.empty() && new_word == game.last_secret && attempts < MAX_ATTEMPTS);

        game.secret = new_word;
        game.last_secret = new_word;
        if (game.round == 0) game.round = 1;
    }
}

//=========================================
// Soft Reset serwera
//=========================================
void game_reset_total(Game &game) {
    game.game_started = false;              // Zatrzymuje silnik gry i wraca do trybu lobby
    game.game_over_state = false;           // Wyłącza stan game_over
    game.game_over_ranking_sent = false;    // Pozwala na ponowne wysłanie rankingu po nowym meczu
    game.warmup_round = true;               // Przywraca tryb rozgrzewki
    game.warmup_guessed = 0;                // Zeruje licznik r (ready)
    game.round = 0;                         // Cofa licznik do początku
    game.guessed.clear();                   // Usuwa zapamiętane litery z poprzedniego meczu
    game.wrong_letters.clear();             // Usuwa błędne litery
    game.secret = "";                       // Czyści hasło
    
    // Resetujemy statystyki graczy, ale zostawiamy ich nicki!
    for (auto &p : game.players) {
        p.second.lives = 5;
        p.second.points = 0;
        p.second.win_points = 0;
        p.second.alive = true;
        p.second.guessed_warmup = false;
    }
}

//=========================================
// Tworzenie gracza
//=========================================
void game_add_player(Game &game, int fd) {
    Player p;
    p.nick = "";
    p.lives = 5;
    p.points = 0;
    p.alive = true;
    p.win_points = 0;
    p.guessed_warmup = false;
    p.last_activity = time(nullptr);
    game.players[fd] = p;
}

//=========================================
// Usuwanie gracza
//=========================================
void game_remove_player(Game &game, int fd) {
    game.players.erase(fd);
}

//=========================================
// Ukrywanie hasła
//=========================================
std::string masked_word(const Game &game) {
    std::string out;
    for (char c : game.secret) {
        if (game.guessed.count(c)) out += c;
        else out += '_';
    }
    return out;
}

//=========================================
// Czy ktoś żyje?
//=========================================
bool game_anyone_alive(const Game &game) {
    for (const auto &p : game.players) {
        // Sprawdzaj tylko graczy, którzy mają nick
        if (!p.second.nick.empty() && p.second.lives > 0) return true;
    }
    return false;
}

//=========================================
// Błędne litery
//=========================================
std::string get_wrong_letters(const Game &game) {
    std::string wrong;
    for (char c : game.wrong_letters) {
        wrong += c;
        wrong += " ";
    }
    if (!wrong.empty()) wrong.pop_back();
    return wrong;
}

//=========================================
// Nagradzanie win_points
//=========================================
void award_win_points(Game &game) {
    if (game.warmup_round) return;
    
    std::vector<std::pair<int, int>> scores;
    for (const auto &p : game.players) scores.push_back({p.first, p.second.points});
    
    std::sort(scores.begin(), scores.end(), [](auto &a, auto &b) { return a.second > b.second; });
    
    int pts[] = {3, 2, 1};
    for (size_t i = 0; i < std::min(size_t(3), scores.size()); i++) {
        game.players[scores[i].first].win_points += pts[i];
    }
    
    if (game.round_winner_fd != -1 && game.players.count(game.round_winner_fd)) {
        game.players[game.round_winner_fd].win_points += 2;
    }
}

//=========================================
// Ranking na koniec rundy
//=========================================
std::string get_rankings(const Game &game) {
    std::vector<std::pair<int, int>> scores;
    for (const auto &p : game.players) scores.push_back({p.first, p.second.points});
    std::sort(scores.begin(), scores.end(), [](auto &a, auto &b) { return a.second > b.second; });
    
    std::stringstream ss;
    ss << "ROUND " << game.round << " RANKINGS:\n";
    for (size_t i = 0; i < scores.size(); i++) {
        const Player &p = game.players.at(scores[i].first);
        ss << (i + 1) << ". " << p.nick << " - " << p.points << " pts\n";
    }
    return ss.str();
}

//=========================================
// Ostateczny ranking
//=========================================
std::string get_final_rankings(const Game &game) {
    std::vector<std::pair<int, int>> scores;
    for (const auto &p : game.players) scores.push_back({p.first, p.second.win_points});
    std::sort(scores.begin(), scores.end(), [](auto &a, auto &b) { return a.second > b.second; });
    
    std::stringstream ss;
    ss << "====== GAME OVER ======\n";
    for (size_t i = 0; i < scores.size(); i++) {
        const Player &p = game.players.at(scores[i].first);
        ss << (i + 1) << ". " << p.nick << " - " << p.win_points << " WIN POINTS\n";
    }
    return ss.str();
}

//=========================================
// Info o graczach dla SFML
//=========================================
std::string get_all_players_stats(const Game &game) {
    std::string stats = "PLAYERS_LIST ";
    bool added = false;
    for (const auto &p : game.players) {
        if (p.second.nick.empty()) continue; // Pomiń graczy bez nicku
        
        stats += p.second.nick + ":" + std::to_string(p.second.lives) + ":" + 
                 std::to_string(p.second.points) + ":" + std::to_string(p.second.win_points) + ",";
        added = true;
    }
    if (added && stats.back() == ',') stats.pop_back();
    return stats + "\n";
}


static int count_players_with_nick(const Game &game) {
    int cnt = 0;
    for (const auto &p : game.players) if (!p.second.nick.empty()) ++cnt;
    return cnt;
}
//=========================================
// Game Handler
//=========================================
std::string game_handle_message(Game &game, int fd, const std::string &msg) {

    // Aktywność gracza
    time_t now = time(nullptr);
    if (game.players.find(fd) != game.players.end()) {
        game.players[fd].last_activity = time(nullptr);
    }

    // ***
    // NICK handling
    // ***
    if (msg.rfind("NICK ", 0) == 0) {
        std::string nick = msg.substr(5);

        // Sprawdza czy nick jest zajęty
        for (auto &p : game.players) {
            if (p.second.nick == nick && p.first != fd) {
                return "NICK_TAKEN\n";
            }
        }

        // Przypisanie nicku oraz czas aktywności
        game.players[fd].nick = nick;
        game.players[fd].last_activity = time(nullptr);
        game.players[fd].nick_set_time = time(nullptr);

        // Logika dołączania w trakcie gry
        if (game.game_started) {
            if (game.warmup_round) {
                game.players[fd].lives = 5;
                game.players[fd].alive = true;
                game.players[fd].points = 0;
                game.players[fd].guessed_warmup = false;
            } else {
                // W normalnej rundzie - gracz musi czekać na następną
                game.players[fd].lives = 0;
                game.players[fd].alive = false;
                game.players[fd].points = 0;
            }
        }

        // Liczenie graczy z nickami
        int players_with_nick = 0;
        for (auto const& [player_fd, p] : game.players) {
            if (!p.nick.empty()) players_with_nick++;
        }

        // Rozpoczęcie gry, jeśli mamy wystarczająco graczy
        if (players_with_nick >= 2 && !game.game_started) {
            printf("=== STARTING WARMUP: %d players ready ===\n", players_with_nick);
            game_init(game);
        }

        // Odpowiedź do gracza, kiedy nick się zgadza
        std::string response = "NICK_OK\n";

        // Jeśli jest za mało graczy to wysyłamy odpowiedni komunikat i jego staty
        if (players_with_nick < 2) {
            response += "WAITING_FOR_PLAYERS Czekamy na " + std::to_string(2-players_with_nick) + " graczy...\n";
            response += "PERSONAL LIVES 5 POINTS 0 WINPTS 0\n";
            return response;
        }
    
        // Wysłanie do nowego gracza aktywnego stanu gry
        if (game.game_started && !game.secret.empty()) {
            std::string masked = masked_word(game);
            std::string wrong = get_wrong_letters(game);
            
            // Wysyłamy do tego jednego klienta a nie wszystkich
            response += "PERSONAL WORD " + masked + " WRONG " + wrong + "\n";
            
            // Liczenie ilu graczy potrzeba do końca rozgrzewki
            if (game.warmup_round && game.round_active) {
                int active_players_count = 0;
                for (const auto &p : game.players) {
                    if (!p.second.nick.empty()) {
                        active_players_count++;
                    }
                }
                int needed = (active_players_count == 2) ? 2 : (active_players_count + 1) / 2;
                response += "PERSONAL Warmup: " + std::to_string(game.warmup_guessed) + 
                        "/" + std::to_string(needed) + " players ready\n";
            }
        } else {
            response += "PERSONAL WORD _ WRONG Waiting for more players...\n";
        }
        
        // Wysyłamy do wszystkich graczy aktualną listę graczy
        response += "BROADCAST " + get_all_players_stats(game);

        // --- WAŻNE: powiadom wszystkich klientów o stanie gry (WORD) żeby GUI wszystkich się zsynchronizowało
       if (game.game_started && !game.secret.empty()) {
           std::string masked_all = masked_word(game);
           std::string wrong_all = get_wrong_letters(game);
           response += "BROADCAST WORD " + masked_all + " WRONG " + wrong_all + "\n";
           // Jeśli to rozgrzewka, dołącz krótki komunikat o stanie warmup
           if (game.warmup_round) {
               response += "BROADCAST Warmup: " + std::to_string(game.warmup_guessed) + " ready\n";
           }
       }

        // Wysyłamy do gracza informacje o jego zasobach
        response += "PERSONAL LIVES " + std::to_string(game.players[fd].lives);
        response += " POINTS " + std::to_string(game.players[fd].points);
        response += " WINPTS " + std::to_string(game.players[fd].win_points) + "\n";
        
        return response;
}


    // ***
    // GUESS handling
    // ***
    if (msg.rfind("GUESS ", 0) == 0) {

        // Sprawdzenie czy nadal jest rozgrzewka
        if (game.warmup_round && !game.round_active) {
            if (now - game.round_start_time >= 10) {
                game.warmup_round = false;
                game.round_active = false;
                return "WARMUP_TIMEOUT\n";
            }
        }

        // Sprawdzenie czy gra się rozpoczęła
        if (!game.game_started) {
            return "ERROR Czekamy na wieksza liczbe graczy z nickami...\n";
        }

        // Liczenie aktywnych graczy (z nickiem)
        int active_players = 0;
        for (const auto &p : game.players) {
            if (!p.second.nick.empty()) active_players++;
        }

        // Jeśli mniej niż 2 graczy i jesteśmy w lobby/rozgrzewce (gra NIE ruszyła) -> blokuj zgadywanie.
        // Jeśli natomiast gra już trwa (game_started==true) i została 1 osoba, pozwól jej kontynuować,
        // aby mogła dokończyć rundę nawet gdy inni wyszli.
        if (active_players < 2 && (game.warmup_round || !game.game_started)) {
            std::string masked = masked_word(game);
            std::string response = "BROADCAST WORD " + masked + " WRONG CZEKAMY_NA_GRACZY\n";
            response += "BROADCAST " + get_all_players_stats(game);
            return response;
        }
        
        // Wyciąganie litery jaką zgaduje gracz
        std::string guess = msg.substr(6);
        guess.erase(guess.find_last_not_of("\r\n") + 1);

        // Sprawdzenie czy runda jest aktywna
        if (!game.round_active) {
            return "WAIT New round starting...\n";
        }

        // Sprawdzenie czy gracz żyje
        if (game.players[fd].lives <= 0) {
            return "DEAD You have no lives left\n";
        }

        // Walidacja inputu
        if (guess.size() != 1) {
            return "ERROR You must guess exactly one letter\n";
        }
        char letter = toupper(guess[0]);
        if (!isalpha(letter)) {
            return "ERROR Guess must be a letter\n";
        }

        // ***
        // ===== Warmup GAME =====
        // ***
        if (game.warmup_round) {

            if (letter != 'R') {
                return "ERROR During warmup, you must guess 'R'\n";
            }
            
            Player &pl = game.players[fd];

            if (pl.guessed_warmup) {
                return "ALREADY_GUESSED You already guessed R!\n";
            }
            
            pl.guessed_warmup = true;
            game.warmup_guessed++;

            // Obliczanie ilu graczy musi zgłosić gotowość
            int active_players_count = 0;
            for (const auto &p : game.players) {
                if (!p.second.nick.empty()) {
                    active_players_count++;
                }
            }
            int needed;
            if (active_players_count == 2) {
                needed = 2;
            } else {
                needed = (active_players_count + 1) / 2;
            }

            // Start Odliczania
            if (game.warmup_guessed >= needed) {
                
                game.warmup_guessed = 0;
                game.round_active = false;
                game.round_start_time = time(nullptr);
                
                game.guessed.clear();
                game.wrong_letters.clear();
                
                // Reset graczy do normalnej gry
                for (auto &p : game.players) {
                    p.second.guessed_warmup = false;
                    p.second.lives = 5;
                    p.second.points = 0;
                    p.second.win_points = 0;
                }

                std::string response = "BROADCAST WARMUP_FINISHED\n";
                response += "BROADCAST Warmup complete! Normal game starts in 10 seconds...\n";
                
                // Wyślij zaktualizowaną listę graczy po warmup
                response += "BROADCAST " + get_all_players_stats(game);
                
                // Wysłanie personalnych stat do każdego
                for (const auto &p : game.players) {
                    response += "PERSONAL_TO_" + std::to_string(p.first) + " LIVES " + 
                    std::to_string(p.second.lives) + " POINTS " + 
                    std::to_string(p.second.points) + " WINPTS " + 
                    std::to_string(p.second.win_points) + "\n";
                }

                return response;
            }
            
            std::string word_display;
            if (pl.guessed_warmup) {
                word_display = "R";
            } else {
                word_display = "_";
            }
            
            // Minimalizuj ruch: PERSONAL WORD tylko do zgadującego, BROADCAST tylko stan warmupu i listę graczy.
            std::string personal = "PERSONAL WORD " + word_display + " WRONG \n";
            personal += "PERSONAL LIVES " + std::to_string(game.players[fd].lives) +
                " POINTS " + std::to_string(game.players[fd].points) + 
                " WINPTS " + std::to_string(game.players[fd].win_points) + "\n";

            std::string broadcast = "BROADCAST Warmup: " + std::to_string(game.warmup_guessed) + 
                        "/" + std::to_string(needed) + " players ready\n";
            broadcast += "BROADCAST " + get_all_players_stats(game);

            return broadcast + personal;
        }

        // ***
        // ===== Normal GAME =====
        // ***
        
        // Sprawdzamy czy odgadnięto już
        if (game.guessed.count(letter)) {
            return "ALREADY_GUESSED\n";
        }

        game.guessed.insert(letter);

        // Sprawdzamy czy trafienie
        bool hit = false;
        for (char c : game.secret) {
            if (c == letter) {
                hit = true;
                game.players[fd].points += 10;
            }
        }
        
        printf("DEBUG: Player %d guessed '%c', hit=%d, total points=%d\n", 
               fd, letter, hit, game.players[fd].points);

        // Kara za miss
        if (!hit) {
            game.players[fd].lives--;
            game.wrong_letters.insert(letter);

            // Sprawdzenie czy to koniec dla wszystkich
            if (!game_anyone_alive(game)) {
                award_win_points(game);

                int players_with_nick = count_players_with_nick(game);
                if (players_with_nick <= 1) {
                    // Przywróć stan lobby/warmup (nie czyścimy nicków)
                    game.warmup_round = true;
                    game.game_started = false;
                    game.round_active = false;
                    game.warmup_guessed = 0;

                    // Przygotuj odpowiedź: ujawnij sekret + powiadom, że czekamy na graczy
                    std::string reveal_secret = game.secret;
                    std::string response = "BROADCAST ALL_LOST " + reveal_secret + "\n";
                    response += broadcast_multiline(get_rankings(game));
                    response += "BROADCAST " + get_all_players_stats(game);
                    response += "BROADCAST WAITING_FOR_PLAYERS Need more players to continue...\n";

                    // Personal stats
                    for (const auto &p : game.players) {
                        response += "PERSONAL_TO_" + std::to_string(p.first) + " LIVES " +
                            std::to_string(p.second.lives) + " POINTS " +
                            std::to_string(p.second.points) + " WINPTS " +
                            std::to_string(p.second.win_points) + "\n";
                    }
                    return response;
                }

                // Sprawdzenie czy to ostatnia runda
                if (game.round >= Game::MAX_ROUNDS) {

                    // Przełączenie się w tryb koniec gry
                    if (!game.game_over_state) {
                        game.game_over_state = true;
                        game.game_over_time = time(nullptr);
                        game.game_over_ranking_sent = false;
                    }
                    
                    // Ujawnienie hasła i ranking końcowy
                    std::string reveal_secret = game.secret;
                    std::string response = "BROADCAST ALL_LOST " + reveal_secret + "\n";
                    response += broadcast_multiline(get_final_rankings(game));
                    
                    // Surowe dane dla SFML
                    response += "BROADCAST " + get_all_players_stats(game);
                    
                    // Rematch timer
                    int wait_left = 60;
                    response += "BROADCAST WAIT_REMATCH " + std::to_string(wait_left) + "\n";
                    
                    // Personal stats
                    for (const auto &p : game.players) {
                        response += "PERSONAL_TO_" + std::to_string(p.first) + " LIVES " + 
                        std::to_string(p.second.lives) + " POINTS " + 
                        std::to_string(p.second.points) + " WINPTS " + 
                        std::to_string(p.second.win_points) + "\n";
                    }
                    
                    return response;
                }
                // ***
                // INTER-ROUND -  Każdy stracił życie, ale gra trwa
                // ***
                game.round++;
                game.round_active = false; // STOP ROUND - wait 10 seconds
                game.round_start_time = time(nullptr); // START COUNTDOWN
                
                // Pobranie hasła i ranking
                std::string reveal_secret = game.secret;
                std::string rankings = get_rankings(game);
                
                // Każdy to loser i pokaż hasło
                std::string response = "BROADCAST ALL_LOST " + reveal_secret + "\n";
                response += broadcast_multiline(rankings);
                
                // Wysłanie zaktualizowanych danych graczy przed resetem
                response += "BROADCAST " + get_all_players_stats(game);
                
                // Wysłanie zaktualizowanych stat przed resetem
                for (const auto &p : game.players) {
                    response += "PERSONAL_TO_" + std::to_string(p.first) + " LIVES " + 
                    std::to_string(p.second.lives) + " POINTS " + 
                    std::to_string(p.second.points) + " WINPTS " + 
                    std::to_string(p.second.win_points) + "\n";
                }
                
                return response;
            }
        }

        // Gracz padł
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
        
        // Sprawdzenie warunku wygranej
        std::string masked = masked_word(game);

        if (masked == game.secret) {
            game.round_active = false; // STOP ROUND
            game.round_winner_fd = fd;
            
            // Przypisanie punktów
            printf("DEBUG: WORD COMPLETE! Winner fd=%d with %d points\n", 
                   fd, game.players[fd].points);
            award_win_points(game);

            printf("DEBUG: After award_win_points, winner has %d win_points\n", 
                   game.players[fd].win_points);

            int players_with_nick = count_players_with_nick(game);
            if (players_with_nick <= 1) {
                // Ustaw tryb warmup/lobby, nie zwiększamy numeru rundy (albo możesz zresetować)
                game.warmup_round = true;
                game.game_started = false;
                game.round_active = false;
                game.warmup_guessed = 0;

                std::string result = "BROADCAST WIN " + game.secret + " ROUND " + std::to_string(game.round) + "\n";
                result += broadcast_multiline(get_rankings(game));
                result += "BROADCAST " + get_all_players_stats(game);
                result += "BROADCAST WAITING_FOR_PLAYERS Need more players to continue...\n";

                for (const auto &p : game.players) {
                    result += "PERSONAL_TO_" + std::to_string(p.first) + " LIVES " + 
                    std::to_string(p.second.lives) + " POINTS " + 
                    std::to_string(p.second.points) + " WINPTS " + 
                    std::to_string(p.second.win_points) + "\n";
                }

                return result;
            }

            int current_round = game.round;

            // Koniec gry, ostatnia runda
            if (game.round >= Game::MAX_ROUNDS) {

                // Aktywacja stanu game_over
                if (!game.game_over_state) {
                    game.game_over_state = true;
                    game.game_over_time = time(nullptr);
                    game.game_over_ranking_sent = false;
                }
                
                std::string result = "BROADCAST WIN " + game.secret + 
                                   " ROUND " + std::to_string(current_round) + "\n";
                result += broadcast_multiline(get_final_rankings(game));
                
                result += "BROADCAST " + get_all_players_stats(game);
                int wait_left = 60;
                result += "BROADCAST WAIT_REMATCH " + std::to_string(wait_left) + "\n";
                for (const auto &p : game.players) {
                    result += "PERSONAL_TO_" + std::to_string(p.first) + " LIVES " + 
                    std::to_string(p.second.lives) + " POINTS " + 
                    std::to_string(p.second.points) + " WINPTS " + 
                    std::to_string(p.second.win_points) + "\n";
                }
                
                return result;
            }
            
            // Przejście między rundami
            game.round++;
            game.round_start_time = time(nullptr); // START 10s COUNTDOWN
            
            // Ranking po win_pointach
            std::string rankings = get_rankings(game);
            
            std::string result = "BROADCAST WIN " + game.secret +
                " ROUND " + std::to_string(current_round) + "\n";
            result += broadcast_multiline(rankings);
            
            // Wysłanie danych przed resetem
            result += "BROADCAST " + get_all_players_stats(game);

            for (const auto &p : game.players) {
                result += "PERSONAL_TO_" + std::to_string(p.first) + " LIVES " + 
                std::to_string(p.second.lives) + " POINTS " + 
                std::to_string(p.second.points) + " WINPTS " + 
                std::to_string(p.second.win_points) + "\n";
            }

            return result;
        }

        // Normal update
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