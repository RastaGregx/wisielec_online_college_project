#include <stdio.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <cstdlib>
#include <poll.h>
#include <sstream>
#include <map>
#include <signal.h>
#include <vector>
#include "game.h"

#define BUFFER_SIZE 256
#define INITIAL_CAPACITY 10

// Mapa buforów klientów
std::map<int, std::string> client_buffers;

// Funkcja pomocnicza do parsowania wiadomości
std::vector<std::string> extract_messages(std::string &buffer) {

    std::vector<std::string> messages;
    size_t pos = 0;

    while ((pos = buffer.find('\n')) != std::string::npos) {
        std::string msg = buffer.substr(0, pos);
        // Usuń znak powrotu karetki, jeśli jest (np. z telnetu)
        if (!msg.empty() && msg.back() == '\r') {
            msg.pop_back();
        }
        if (!msg.empty()) {
            messages.push_back(msg);
        }
        buffer.erase(0, pos + 1);
    }
    return messages;
}

// Funkcja resetująca stan gry
void reset_game_completely(Game &game) {
    // NIE czyść game.players.clear(), jeśli wywołujesz to przy nfds > 1
    game.guessed.clear();
    game.wrong_letters.clear();
    // Resetuj tylko flagi globalne
    game.secret = "";
    game.round = 0;
    game.game_started = false;
    game.round_active = false;
    game.warmup_round = true;
    game.game_over_state = false;
}

void universal_reset(Game &game, bool clear_players) {
    // 1. Część wspólna dla obu resetów
    game.guessed.clear();
    game.wrong_letters.clear();
    game.round = 0;
    game.game_over_state = false;
    game.warmup_round = true;
    game.warmup_guessed = 0;

    if (clear_players) {
        // Scenariusz: Pusty serwer (Reset 1)
        game.players.clear();
        game.game_started = false;
    } else {
        // Scenariusz: Nowa partia dla tych samych osób (Reset 2)
        for (auto &p : game.players) {
            p.second.lives = 5;
            p.second.points = 0;
            p.second.alive = true;
            // Nicków NIE czyścimy
        }
    }
}

int main()
{
    // Wyłączenie buforowania stdout i ignorowanie SIGPIPE
    setvbuf(stdout, NULL, _IONBF, 0);
    signal(SIGPIPE, SIG_IGN); 

    sockaddr_in server{};
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = htonl(INADDR_ANY); 
    server.sin_port = htons(1234);

    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) {
        perror("Socket error");
        return 1;
    }

    int opt = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt error");
        return 1;
    }

    if (bind(fd, (struct sockaddr *)&server, sizeof(server)) < 0) {
        perror("Bind error");
        return 1;
    }

    listen(fd, SOMAXCONN);
    printf("Server is listening on port 1234...\n");

    //Inicjalizacja Gry
    Game game;
    game.round = 0;
    game.warmup_round = true; 
    game_init(game);

    //Alokacja Tablicy Monitorującej
    int capacity = INITIAL_CAPACITY;
    struct pollfd *fds = (struct pollfd *)malloc(capacity * sizeof(struct pollfd));
    if (!fds) {
        perror("Memory allocation problem");
        close(fd);
        return 1;
    }
    int nfds = 1;           //Ile gniazd obserwujemy
    fds[0].fd = fd;         //Nasze gniazdo na którym nasłuchujemy
    fds[0].events = POLLIN; 

    // ===== WAŻNE: ZMIENNE STANU LICZNIKÓW PRZED PĘTLĄ WHILE =====
    // Jeśli umieścisz je w środku bez 'static', będą się resetować co obrót pętli
    // i spamować klientów, powodując lagi (efekt FIFO).


    // Pamięci poprzedniego stanu
    int last_warmup_countdown = -1;
    int last_inter_round_countdown = -1;
    int last_game_over_countdown = -1;
    int last_round_time_sent = -1;

    while (1) {

        //  Obserwowanie przez system operacyjne wszystkich socketów w tej tabeli
        int ret = poll(fds, nfds, 1000);
        if (ret < 0) {
            perror("poll");
            break;
        }

        //=========================================
         //  Reset gry gdy nie ma klientów, nfds == 1 oznacza tylko socket serwera
        //=========================================
        if (nfds == 1) {
            // Sprawdzamy, czy gra ma jakieś pozostałości
            if (!game.players.empty() || game.game_started || game.warmup_guessed > 0 || game.round > 0) {
                universal_reset(game, true);
                
                // Reset liczników po resecie gry
                last_warmup_countdown = -1;
                last_inter_round_countdown = -1;
                last_game_over_countdown = -1;
                last_round_time_sent = -1;
            }
        }

        //=========================================
        // Czyszczenie nieaktywnych graczy
        //=========================================
        time_t now = time(nullptr); // Punkt odniesienia czy gracz wykonał jakąś akcję
        for (int i = 1; i < nfds; i++) {

            int cfd = fds[i].fd;
            if (game.players.count(cfd)) {
                // Jeśli gracz nic nie robi przez 300s, wyrzuć go
                if (!game.game_over_state && now - game.players[cfd].last_activity > 300) {
                    printf("Gracz %d wyrzucony za bezczynność\n", cfd);
                    send(cfd, "DISCONNECT_TIMEOUT\n", 19, 0);
                    game_remove_player(game, cfd);
                    client_buffers.erase(cfd);
                    close(cfd);
                    fds[i] = fds[nfds - 1];
                    nfds--;
                    i--;
                }
            }
        }

        //=========================================
        // Automatyczny restart po zakończeniu gry
        //=========================================
        if (game.game_over_state) {
            int elapsed = now - game.game_over_time;
            int remaining = 60 - elapsed;
            if (remaining < 0) remaining = 0;

            // Ograniczenie ruchu: wysyłamy status rematch co 10s oraz przy 0s
            if (((remaining % 10 == 0) || remaining == 0) && remaining != last_game_over_countdown) {
                last_game_over_countdown = remaining;
                std::string countdown_msg = "WAIT_REMATCH " + std::to_string(remaining) + "\n";
                for (int j = 1; j < nfds; j++) {
                    write(fds[j].fd, countdown_msg.c_str(), countdown_msg.size());
                }
            }

            // Moment restartu gry
            if (now - game.game_over_time >= 61) {
                printf("=== RESTARTUJĘ GRĘ DLA POŁĄCZONYCH GRACZY ===\n");

                // UŻYWAMY FUNKCJI (false = zachowaj nicki graczy)
                universal_reset(game, false); 
                
                // Resetujemy liczniki powiadomień
                last_warmup_countdown = -1;
                last_inter_round_countdown = -1;
                last_game_over_countdown = -1;
                last_round_time_sent = -1;
                

                int players_ready = 0;
                for (auto &p : game.players) {
                    if (!p.second.nick.empty()) players_ready++;
                }

                if (players_ready >= 2) {
                    game_init(game); 
                    last_round_time_sent = -1; 
                } else {
                    game.game_started = false; 
                }

                // Przygotowanie i wysyłka pakietu startowego
                std::string reset_packet = "REMATCH_OK\n";
                if (game.game_started) {
                    std::string masked = masked_word(game);
                    std::string wrong = get_wrong_letters(game);
                    reset_packet += "WORD " + masked + " WRONG " + wrong + "\n";
                    reset_packet += get_all_players_stats(game);
                } else {
                    reset_packet += "WAITING_FOR_PLAYERS Need more players...\n";
                }
                
                for (int j = 1; j < nfds; j++) {
                    int cfd = fds[j].fd;
                    write(cfd, reset_packet.c_str(), reset_packet.size());
                    if (game.players.count(cfd)) {
                        std::string personal = "LIVES " + std::to_string(game.players[cfd].lives) + 
                                            " POINTS " + std::to_string(game.players[cfd].points) + 
                                            " WINPTS " + std::to_string(game.players[cfd].win_points) + "\n";
                        write(cfd, personal.c_str(), personal.size());
                    }
                }
                
                // Opcjonalnie: Wyślij czas rundy od razu, jeśli gra ruszyła
                if (game.game_started) {
                    std::string timer_msg = "ROUND_TIME 60\n";
                    for (int j = 1; j < nfds; j++) write(fds[j].fd, timer_msg.c_str(), timer_msg.size());
                }
            }
        } // Tu kończy się blok restartu

        //=========================================
        // Pauza między rundami
        //=========================================
        if (!game.warmup_round && !game.round_active && game.game_started && !game.game_over_state) { 

            // ***
            // Odliczanie
            // ***
            int elapsed = now - game.round_start_time;
            int remaining = 10 - elapsed;
            if (remaining < 0) remaining = 0;            
            if (remaining != last_inter_round_countdown && remaining > 0) {
                last_inter_round_countdown = remaining;
                std::string countdown_msg = "INTER_ROUND_COUNTDOWN " + std::to_string(remaining) + "\n";
                for (int j = 1; j < nfds; j++) {
                    write(fds[j].fd, countdown_msg.c_str(), countdown_msg.size());
                }
            }

            // ***
            // Przygotowanie nowej gry po skończeniu odliczania
            // ***
            if (elapsed >= 10) {
                printf("=== 10 SECONDS PASSED - STARTING NEW ROUND ===\n");
                last_inter_round_countdown = -1;
                
                // Reset gracza
                for (auto &p : game.players) {
                    p.second.lives = 5;
                    p.second.points = 0;
                    p.second.guessed_warmup = false;
                }
                
                // Losowanie nowego hasła
                game_init(game);

                // Wysłanie czasu rundy
                std::string timer_msg = "ROUND_TIME 60\n";
                for (int k = 1; k < nfds; k++) {
                    int target_fd = fds[k].fd;
                    if (game.players.count(target_fd) && !game.players[target_fd].nick.empty()) {
                        write(target_fd, timer_msg.c_str(), timer_msg.size());
                    }
                }

                std::string masked = masked_word(game);
                std::string wrong = get_wrong_letters(game);
                
                // Wysłanie wiadomości do graczy nowej rundzie, zamaskowanym haśle, lista błędnych liter oraz aktualny ranking
                std::string msg = "BROADCAST NEW_ROUND\n";
                msg += "BROADCAST WORD " + masked + " WRONG " + wrong + "\n";
                msg += "BROADCAST " + get_all_players_stats(game) + "\n";            
                
                // Wysłanie statystyk personalnych by każdy z graczy miał dobrze sfml
                for (int j = 1; j < nfds; j++) {
                    int cfd = fds[j].fd;
                    std::string current_msg = msg; // Kopia bazy broadcastu
                    if (game.players.count(cfd)) {
                        current_msg += "LIVES " + std::to_string(game.players[cfd].lives) + " " +
                                    "POINTS " + std::to_string(game.players[cfd].points) + " " +
                                    "WINPTS " + std::to_string(game.players[cfd].win_points) + "\n";
                    }
                    write(cfd, current_msg.c_str(), current_msg.size());
                }
            }
        }


        //=========================================
        // WarmUp
        //=========================================
        if (game.warmup_round && !game.round_active && game.game_started) {
            int elapsed = now - game.round_start_time;
            int remaining = 10 - elapsed;
            if (remaining < 0) remaining = 0;

        // ***
        // Wysyłanie odliczania rozgrzewki
        // ***
        if (remaining != last_warmup_countdown) {
            last_warmup_countdown = remaining;

            std::string packet = "WARMUP_COUNTDOWN " + std::to_string(remaining) + "\n";
            std::string players = get_all_players_stats(game);

            for (int j = 1; j < nfds; j++) {
                int cfd = fds[j].fd;
                std::string out = packet;
                out += players;
                write(cfd, out.c_str(), out.size());
            }
        }

        // ***
        // Zakończenie rozgrzewki
        // ***
            if (elapsed >= 10) {
                printf("=== WARMUP ZAKOŃCZONY - START NORMALNEJ RUNDY ===\n");
                last_warmup_countdown = -1;
                game.warmup_round = false;
                
                // Przygotuj rundę
                game_init(game); // Wylosowanie nowego hasła
                last_round_time_sent = -1;

                // 1) Wyślij NEW_ROUND do wszystkich (sygnal startu)
                std::string newround_msg = "BROADCAST NEW_ROUND\n";
                for (int j = 1; j < nfds; j++) {
                    int cfd = fds[j].fd;
                    // Wyślij każdemu (personalne staty dodamy dalej)
                    write(cfd, newround_msg.c_str(), newround_msg.size());
                }

                // 2) Wyślij timer rundy (ROUND_TIME 60) do graczy z nickiem
                std::string timer_msg = "ROUND_TIME 60\n";
                for (int k = 1; k < nfds; k++) {
                    int target_fd = fds[k].fd;
                    if (game.players.count(target_fd) && !game.players[target_fd].nick.empty()) {
                        write(target_fd, timer_msg.c_str(), timer_msg.size());
                    }
                }

                // 3) Wyślij zamaskowane słowo + lista graczy, a potem personalne staty
                std::string masked = masked_word(game);
                std::string wrong = get_wrong_letters(game);
                
                std::string base_msg = "BROADCAST WORD " + masked + " WRONG " + wrong + "\n";
                base_msg += "BROADCAST " + get_all_players_stats(game) + "\n";            

                for (int j = 1; j < nfds; j++) {
                    int cfd = fds[j].fd;
                    std::string current_msg = base_msg;
                    if (game.players.count(cfd)) {
                        current_msg += "LIVES " + std::to_string(game.players[cfd].lives) + " " +
                                       "POINTS " + std::to_string(game.players[cfd].points) + " " +
                                       "WINPTS " + std::to_string(game.players[cfd].win_points) + "\n";
                    }
                    write(cfd, current_msg.c_str(), current_msg.size());
                }
            }
        }


        //=========================================
        // Timer Rundy (60s, co 10s wysyła) i ukaranie graczy
        //=========================================
            if (game.round_active && game.game_started) {
                int remaining = Game::ROUND_TIME_LIMIT - (now - game.round_start_time);
                if (remaining < 0) remaining = 0;
                
                // Wysyłanie co 10s, ostatnie 10s co 1s
                bool shouldSend = false;
                if (last_round_time_sent == -1 && remaining == 60) {
                    shouldSend = true; // Start rundy
                } else if (remaining % 10 == 0 && remaining != last_round_time_sent && remaining > 10) {
                    shouldSend = true; // Co 10 sekund (60, 50, 40, 30, 20)
                } else if (remaining <= 10 && remaining != last_round_time_sent) {
                    shouldSend = true; // Ostatnie 10 sekund (10, 9, 8, 7...)
                }

                if (shouldSend) {
                    last_round_time_sent = remaining;
                    std::string time_msg = "ROUND_TIME " + std::to_string(remaining) + "\n";
                    for (int j = 1; j < nfds; j++) {
                        int target_fd = fds[j].fd;
                        if (game.players.count(target_fd) && !game.players[target_fd].nick.empty()) {
                            write(target_fd, time_msg.c_str(), time_msg.size());
                        }
                    }
                }

                // ***
                // Koniec czasu rundy - ukaranie graczy
                // ***
                if (now - game.round_start_time >= Game::ROUND_TIME_LIMIT) {
                    printf("=== 60 sekund minęło - wszyscy tracą życia ===\n");
                    last_round_time_sent = -1;
                    for (auto &p : game.players) {
                        if (!p.second.nick.empty()) {
                            p.second.lives = 0;
                        }
                    }
                    game.round_active = false;
                    game.round_start_time = time(nullptr);
                    award_win_points(game);
                    std::string rankings = get_rankings(game);
                    std::string reveal_secret = game.secret;
                    std::string broadcast_msg = "ALL_LOST " + reveal_secret + "\n";
                    broadcast_msg += rankings;
                    game.round++;

                    for (int j = 1; j < nfds; j++) {
                        write(fds[j].fd, broadcast_msg.c_str(), broadcast_msg.size());
                        if (game.players.count(fds[j].fd)) {
                            const Player &p = game.players[fds[j].fd];
                            std::string personal = "LIVES " + std::to_string(p.lives) + 
                                                " POINTS " + std::to_string(p.points) + 
                                                " WINPTS " + std::to_string(p.win_points) + "\n";
                            write(fds[j].fd, personal.c_str(), personal.size());
                        }
                    }
                }
        }


        //=========================================
        // Obsługa nowych połączeń
        //=========================================
        if (fds[0].revents & POLLIN) {

            sockaddr_in client_data{};
            socklen_t len = sizeof(client_data);
            int client = accept(fd, (struct sockaddr *)&client_data, &len);
            if (client < 0) {
                perror("Accept error");
                continue;
            }

            // Dodawanie gracza do gry
            game_add_player(game, client);

            // Czyszczenie danych by nie było śmieci po stary kliencie
            client_buffers[client] = "";
            printf("New connection: %s:%d (fd=%d, total=%d)\n",
                inet_ntoa(client_data.sin_addr), ntohs(client_data.sin_port), client, nfds);

            // Jeśli skończyło się miejsce w tablicy(10), to ją powiększamy
            if (nfds >= capacity) {
                capacity *= 2;
                struct pollfd *tmp = (struct pollfd *)realloc(fds, capacity * sizeof(struct pollfd));
                if (!tmp) {
                    perror("Realloc error");
                    close(client);
                    client_buffers.erase(client);
                    continue;
                }
                fds = tmp;
            }

            // Dodanie klienta do listy obserwowanych
            fds[nfds].fd = client;
            fds[nfds].events = POLLIN;
            nfds++;

            // Wiadomość powitalna
            std::string welcome = "CONNECTED\n";
            int players_with_nick = 0;
            for (const auto &p : game.players) {
                if (!p.second.nick.empty()) players_with_nick++;
            }

            // Wiadomość jeśli gra jest w toku/trybie oczekiwania
            if (game.game_started) {
                welcome += "GAME_IN_PROGRESS Join next round\n";
            } else if (players_with_nick >= 1) {
                welcome += "WAITING_FOR_PLAYERS " + std::to_string(players_with_nick) + 
                        " player(s) in lobby. Need " + 
                        std::to_string(2 - players_with_nick) + " more.\n";
            } else {
                welcome += "WAITING_FOR_PLAYERS Lobby empty. Waiting for players...\n";
            }

            // Informowanie innych graczy, że nowy gracz dołączył
            if (nfds > 1) {
                std::string broadcast = "PLAYER_JOINED Player connected. Total: " + 
                                    std::to_string(game.players.size()) + "\n";
                for (int j = 1; j < nfds; j++) {
                    if (fds[j].fd != client) {
                        write(fds[j].fd, broadcast.c_str(), broadcast.size());
                    }
                }
            }

            write(client, welcome.c_str(), welcome.size());
        }



        //=========================================
        // Obsługa danych od klientów
        //=========================================
        for (int i = 1; i < nfds; i++) {
            if (!(fds[i].revents & POLLIN)) continue; // Sprawdzanie każdego klienta po kolei czy są nowe dane
            int cfd = fds[i].fd;
            char buf[BUFFER_SIZE];
            int bytes = read(cfd, buf, BUFFER_SIZE - 1);

            // ***
            // Rozłączenie
            // ***
            if (bytes <= 0) {
                if (bytes == 0) printf("Client fd=%d disconnected\n", cfd);

                else perror("Read error");
                game_remove_player(game, cfd);
                client_buffers.erase(cfd);
                close(cfd);

                // Przesunięcie tablicy
                fds[i] = fds[nfds - 1];
                nfds--;
                fds[i].revents = 0; 
                i--; 
                continue;
            }

            // ***
            // Odczyt danych
            // ***

            buf[bytes] = '\0';
            client_buffers[cfd] += std::string(buf);
            std::vector<std::string> messages = extract_messages(client_buffers[cfd]);

            for (const std::string &msg : messages) {
                std::string response = game_handle_message(game, cfd, msg); //game_handle_message interpretuje co gracz chce
                if (response.empty()) continue;

                std::istringstream iss(response);
                std::string line;
                std::string broadcast_msg;
                std::string personal_msg;

                while (std::getline(iss, line)) {

                    if (line.empty()) continue;

                    if (line.rfind("BROADCAST ", 0) == 0) { // Do wszystkich
                        broadcast_msg += line.substr(10) + "\n";
                    }

                    else if (line.rfind("PERSONAL_TO_", 0) == 0) { // Do konkretnego gracza, komunikacja między graczami
                        size_t sp = line.find(' ', 12);
                        if (sp != std::string::npos) {
                            int target = std::stoi(line.substr(12, sp - 12));
                            std::string out = line.substr(sp + 1) + "\n";

                            for (int k = 1; k < nfds; k++) {
                                if (fds[k].fd == target) {
                                    write(fds[k].fd, out.c_str(), out.size());
                                    break;
                                }
                            }
                        }
                    }

                    else if (line.rfind("PERSONAL ", 0) == 0) { // Tylko do autora wiadomości
                        personal_msg += line.substr(9) + "\n";
                    }

                    else {
                        personal_msg += line + "\n";
                    }
                }


                // Wysyłanie broadcastem do graczy co mają nick
                if (!broadcast_msg.empty()) {
                    for (int j = 1; j < nfds; j++) {
                        int target_fd = fds[j].fd;
                        if (game.players.count(target_fd) && !game.players[target_fd].nick.empty()) {
                            write(target_fd, broadcast_msg.c_str(), broadcast_msg.size());
                        }
                    }
                }

                // Wysyłanie personal
                if (!personal_msg.empty()) {
                    write(cfd, personal_msg.c_str(), personal_msg.size());
                }
            }
        }

    }
    
    // Cleanup przy wyjściu z serwera (CTRL+C)
    for (int i = 0; i < nfds; i++) {
        close(fds[i].fd);
    }
    free(fds);
    return 0;
}