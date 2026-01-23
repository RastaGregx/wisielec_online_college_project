#include <stdio.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <cstdlib>
#include <poll.h> 
#include <SFML/Graphics.hpp>
#include <fcntl.h>
#include <string>
#include <sstream>
#include <signal.h>

int mode = 0;                               // 0 = Inputting Nick, 1 = Game, 2 = Round End Waiting, 3 = Game Over, 4 = Waiting for players
sf::Text* promptPtr = nullptr;              
sf::Text* wordDisplayPtr = nullptr;
sf::Text* livesDisplayPtr = nullptr;
sf::Text* pointsDisplayPtr = nullptr;
sf::Text* wrongLettersPtr = nullptr;
sf::Text* winPointsDisplayPtr = nullptr;    
sf::Text* playersListPtr = nullptr;
sf::Text* roundTimerPtr = nullptr;

sf::Clock roundTimerClock;                  // Stoper SFML czas do końca rundy
int currentRoundTime = 60;
int lastServerRoundTime = 60;

sf::Clock roundEndClock;                    // Stoper SFML czas między rundami
bool roundEndTimerActive = false;
int lastDisplayedSeconds = 10;
bool isWinRound = false;

std::string leftover = "";                  // Komunikacja

int remainingWaitTime = 0;                  // Restart
sf::Clock countdownClock;
bool rematchRequested = false;

sf::Clock inactivityClock;                  // Zegar bezczynności
sf::Text* inactivityTimerPtr = nullptr;
int lastInactivitySeconds = 300;

int net_connection(const char* ip_address, int port) {
    sockaddr_in client_data{};
    client_data.sin_family = AF_INET;
    client_data.sin_addr.s_addr = inet_addr(ip_address);
    client_data.sin_port = htons(port);

    int my_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (my_fd < 0) {
        printf("Error creating socket: %s\n", strerror(errno));
        return -1;
    }

    // [CRITICAL FIX] Set Non-Blocking BEFORE connecting to handle timeouts
    int fd_flags = fcntl(my_fd, F_GETFL);
    if (fcntl(my_fd, F_SETFL, fd_flags | O_NONBLOCK) < 0) {
        printf("Failed to set non-blocking: %s\n", strerror(errno));
        close(my_fd);
        return -1;
    }

    // Start connecting
    int res = connect(my_fd, (struct sockaddr*)&client_data, sizeof(client_data));
    
    if (res < 0) {
        if (errno == EINPROGRESS) {
            // Connection is in progress, wait for it (Timeout: 5 seconds)
            printf("Connecting to %s... (Timeout: 5s)\n", ip_address);
            
            struct pollfd pfd;
            pfd.fd = my_fd;
            pfd.events = POLLOUT;
            
            int poll_res = poll(&pfd, 1, 5000); // 5000 ms timeout
            
            if (poll_res > 0) {
                // Check if connection was actually successful
                int so_error;
                socklen_t len = sizeof(so_error);
                getsockopt(my_fd, SOL_SOCKET, SO_ERROR, &so_error, &len);
                
                if (so_error == 0) {
                    printf("Connected!\n");
                    return my_fd;
                } else {
                    printf("Connection failed (SO_ERROR): %s\n", strerror(so_error));
                    close(my_fd);
                    return -1;
                }
            } else if (poll_res == 0) {
                printf("Connection timed out (Firewall or wrong IP?)\n");
                close(my_fd);
                return -1;
            } else {
                printf("Poll error: %s\n", strerror(errno));
                close(my_fd);
                return -1;
            }
        } else {
            printf("Could not connect immediately: %s\n", strerror(errno));
            close(my_fd);
            return -1;
        }
    }

    return my_fd;
}

void full_reset_client() {
    mode = 0;
    roundEndTimerActive = false;
    isWinRound = false;
    leftover = "";
    rematchRequested = false;
    remainingWaitTime = 300;
    countdownClock.restart();

    if (wordDisplayPtr) wordDisplayPtr->setString("");
    if (promptPtr) {
        promptPtr->setString("Enter your nickname below:");
        promptPtr->setFillColor(sf::Color::Black);
    }
}

void reset_game_display() {
    if (wordDisplayPtr) {
        wordDisplayPtr->setString("");
        wordDisplayPtr->setFillColor(sf::Color::Black);
    }
    if (promptPtr) {
        promptPtr->setString("Guess a letter:");
        promptPtr->setFillColor(sf::Color::Black);
        sf::FloatRect r = promptPtr->getLocalBounds();
        promptPtr->setOrigin(r.left + r.width / 2.f,
                            r.top + r.height / 2.f);
        promptPtr->setPosition(800 / 2.f, 200);
    }
    if (livesDisplayPtr) {
        livesDisplayPtr->setString("Lives: 5");
    }
    if (pointsDisplayPtr) {
        pointsDisplayPtr->setString("Points: 0");
    }
    if (wrongLettersPtr) {
        wrongLettersPtr->setString("Wrong: ");
    }
}

inline void centerText(sf::Text* t, float x, float y) {
    if (!t) return;
    sf::FloatRect r = t->getLocalBounds();
    t->setOrigin(r.left + r.width / 2.f, r.top + r.height / 2.f);
    t->setPosition(x, y);
}

enum class ServerMsgType {
    NickOk,
    NickTaken,
    Word,
    Lives,
    Win,
    AllLost,
    GameOver,
    WaitRematch,
    RematchOk,
    PlayersList,
    AlreadyGuessed,
    YouLost,
    WaitingForPlayers,
    RoundTime,
    NewRound,
    WarmupFinished,        
    WarmupCountdown,
    Connected,
    PlayerJoined,
    Unknown
};

ServerMsgType parseMessageType(const std::string& line) {
    if (line.rfind("NICK_OK",0)== 0) return ServerMsgType::NickOk;
    if (line.rfind("NICK_TAKEN",0)== 0) return ServerMsgType::NickTaken;
    if (line.rfind("WORD ",0)== 0) return ServerMsgType::Word;
    if (line.rfind("LIVES ",0)== 0) return ServerMsgType::Lives;
    if (line.rfind("WIN ",0)== 0) return ServerMsgType::Win;
    if (line.rfind("ALL_LOST",0)== 0) return ServerMsgType::AllLost;
    
    // NEW_ROUND detection - BEFORE other checks
    if (line.rfind("NEW_ROUND",0)== 0) return ServerMsgType::NewRound;
    
    // WARMUP_FINISHED detection
    if (line.rfind("WARMUP_FINISHED",0)== 0) return ServerMsgType::WarmupFinished;
    
    // WARMUP_COUNTDOWN detection
    if (line.rfind("WARMUP_COUNTDOWN",0)== 0) return ServerMsgType::WarmupCountdown;
    
    // Wykrywanie początku GAME_OVER
    if (line.find("====== GAME OVER ======") != std::string::npos) {
        if (playersListPtr) {
            playersListPtr->setString(""); // Reset listy dla rankingu
        }
        return ServerMsgType::GameOver;
    }
    
    if (line.rfind("WAIT_REMATCH",0)== 0) return ServerMsgType::WaitRematch;
    if (line.rfind("REMATCH_OK",0)== 0) return ServerMsgType::RematchOk;
    if (line.rfind("PLAYERS_LIST",0)== 0) return ServerMsgType::PlayersList;
    if (line.rfind("ALREADY_GUESSED",0)== 0) return ServerMsgType::AlreadyGuessed;
    if (line.rfind("ROUND_TIME ",0)== 0) return ServerMsgType::RoundTime;
    if (line.rfind("YOU_LOST", 0) == 0 || line.rfind("LOSE", 0) == 0 || line.rfind("DEAD", 0) == 0) return ServerMsgType::YouLost;
    if (line.rfind("Czekamy na", 0) == 0) return ServerMsgType::WaitingForPlayers;
    if (line.rfind("CONNECTED",0)== 0) return ServerMsgType::Connected;
    if (line.rfind("PLAYER_JOINED",0)== 0) return ServerMsgType::PlayerJoined;
    
    // Wykrywanie linii rankingu finalnego - ZAWSZE gdy mode == 3
    if (mode == 3) {
        // IGNORUJ stare rankingi rundowe
        if (line.find("ROUND") != std::string::npos && line.find("RANKINGS:") != std::string::npos) {
            return ServerMsgType::Unknown;
        }
        
        // IGNORUJ linie ze statystykami rundy (pts, [TOP3], [WINNER])
        if ((line.find("pts") != std::string::npos && line.find("WIN POINTS") == std::string::npos) ||
            (line.find("[TOP3]") != std::string::npos) ||
            (line.find("[WINNER]") != std::string::npos && line.find("[CHAMPION!]") == std::string::npos)) {
            return ServerMsgType::Unknown;
        }
        
        // Sprawdź czy to linia rankingu
        bool isRankingLine = false;
        
        if (line.find("FINAL RANKINGS:") != std::string::npos ||
            line.find("CHAMPION") != std::string::npos ||
            line.find("Place]") != std::string::npos ||
            line.find("WIN POINTS") != std::string::npos ||
            line.find("Thanks for playing") != std::string::npos) {
            isRankingLine = true;
        }
        
        // Sprawdź czy to linia zaczynająca się od numeru (1., 2., 3., etc.) I MA "WIN POINTS"
        if (!line.empty() && line.length() >= 2) {
            if (line[0] >= '1' && line[0] <= '9' && line[1] == '.' && line.find("WIN POINTS") != std::string::npos) {
                isRankingLine = true;
            }
        }
        
        if (isRankingLine) {
            // Dodaj do listy graczy (która teraz pokazuje ranking)
            if (playersListPtr && !line.empty()) {
                std::string current = playersListPtr->getString();
                playersListPtr->setString(current + line + "\n");
            }
            return ServerMsgType::Unknown;
        }
    }
    
    return ServerMsgType::Unknown;
}

void handleConnected(const std::string& line) {
    mode = 4; // LOBBY MODE
    
    if (promptPtr) {
        promptPtr->setString("Enter your nickname to join:");
        promptPtr->setFillColor(sf::Color::Black);
        centerText(promptPtr, 800 / 2.f, 200);
    }
    
    if (wordDisplayPtr) wordDisplayPtr->setString("");
    if (wrongLettersPtr) wrongLettersPtr->setString("");
    if (livesDisplayPtr) livesDisplayPtr->setString("Lives: 5");
    if (pointsDisplayPtr) pointsDisplayPtr->setString("Points: 0");
    if (winPointsDisplayPtr) winPointsDisplayPtr->setString("Win: 0");
}

void handlePlayerJoined(const std::string& line) {
    if (mode == 4) { // tylko w lobby
        std::string msg = line.substr(14); // "PLAYER_JOINED ..."
        printf("[LOBBY] %s\n", msg.c_str());
    }
}

void handleNewRound() {
    // Przywróć normalny tryb gry i zresetuj lokalny timer rundy
    mode = 1;
    roundEndTimerActive = false;
    isWinRound = false;

    // Synchronizacja timera lokalnego (serwer też prześle ROUND_TIME 60)
    lastServerRoundTime = 60;
    roundTimerClock.restart();

    if (promptPtr) {
        promptPtr->setString("Guess a letter:");
        promptPtr->setFillColor(sf::Color::Black);
        centerText(promptPtr, 800 / 2.f, 200);
    }

    if (wordDisplayPtr) {
        wordDisplayPtr->setFillColor(sf::Color::Black);
        // Nie czyścimy słowa — serwer wyśle WORD natychmiast po NEW_ROUND
    }
    if (wrongLettersPtr) wrongLettersPtr->setString("Wrong: ");
    inactivityClock.restart();
}

void handleWarmupFinished() {
    
    if (promptPtr) {
        promptPtr->setString("Warmup complete! Starting in 10 seconds...");
        promptPtr->setFillColor(sf::Color::Green);
        centerText(promptPtr, 800 / 2.f, 200);
    }
}

void handleWarmupCountdown(const std::string& line) {
    std::istringstream iss(line);
    std::string token;
    int remaining;
    iss >> token >> remaining; // "WARMUP_COUNTDOWN 9"
    
    
    if (promptPtr) {
        promptPtr->setString("Normal game starts in " + std::to_string(remaining) + " seconds...");
        promptPtr->setFillColor(sf::Color::Green);
        centerText(promptPtr, 800 / 2.f, 200);
    }
}


void handleRoundTime(const std::string& line) {
    std::istringstream iss(line);
    std::string token;
    int remaining;
    iss >> token >> remaining; // "ROUND_TIME 60"
    
    // Synchronizuj z serwerem
    lastServerRoundTime = remaining;
    roundTimerClock.restart(); // Reset lokalnego timera
    
    if (roundTimerPtr) {
        if (remaining <= 10) {
            roundTimerPtr->setFillColor(sf::Color::Red);
        } else if (remaining <= 20) {
            roundTimerPtr->setFillColor(sf::Color(255, 165, 0));
        } else {
            roundTimerPtr->setFillColor(sf::Color::Black);
        }
        roundTimerPtr->setString("Round: " + std::to_string(remaining) + "s");
    }
}

void handleNickOk() {
    inactivityClock.restart(); 
    if (promptPtr) {
        promptPtr->setString("Waiting for more players...");
        promptPtr->setFillColor(sf::Color(255, 165, 0)); // Orange
        centerText(promptPtr, 800 / 2.f, 200);
    }
}

void handleNickTaken() {
    mode = 0;
    if (promptPtr) {
        promptPtr->setString("Nickname taken! Enter another:");
        centerText(promptPtr, 800 / 2.f, 250);
    }
}

// W funkcji handleWord() zamień na:

void handleWord(const std::string& line) {
    if (mode == 3) return; // ignore word updates during GAME_OVER
    if (mode == 4) {
        mode = 1; // Gra się zaczęła!
    }

    std::istringstream iss(line);
    std::string token, word;
    iss >> token >> word;

    // Collect wrong letters if present
    std::string wrong_letters;
    iss >> token; 
    if (token == "WRONG") {
        std::string letter;
        std::string rest;
        std::getline(iss, rest); // Pobierz całą resztę linii
        wrong_letters = rest;
        
        // Usuń wiodące spacje
        size_t start = wrong_letters.find_first_not_of(" \t\r\n");
        if (start != std::string::npos) {
            wrong_letters = wrong_letters.substr(start);
        }
        
        // Usuń końcowe spacje
        size_t end = wrong_letters.find_last_not_of(" \t\r\n");
        if (end != std::string::npos) {
            wrong_letters = wrong_letters.substr(0, end + 1);
        }
    }


    // Wyświetl słowo (nawet jeśli to tylko "_")
    if (wordDisplayPtr) {
        std::string spaced_word;
        for (size_t i = 0; i < word.size(); i++) {
            spaced_word += word[i];
            if (i < word.size() - 1) spaced_word += " ";
        }
        wordDisplayPtr->setString(spaced_word);
        
        // Zachowaj kolor jeśli jesteśmy w trybie oczekiwania (mode 2)
        if (mode != 2) {
            wordDisplayPtr->setFillColor(sf::Color::Black);
        }
        centerText(wordDisplayPtr, 800 / 2.f, 150);
    }

    // Wyświetl wrong letters (włącznie z komunikatami typu "Waiting for more players...")
    if (wrongLettersPtr) {
        if (wrong_letters.empty()) {
            wrongLettersPtr->setString("Wrong: ");
            wrongLettersPtr->setFillColor(sf::Color::Red);
        } else if (wrong_letters.find("Waiting") != std::string::npos || 
                   wrong_letters.find("waiting") != std::string::npos) {
            // Jeśli to komunikat oczekiwania, wyświetl go w kolorze pomarańczowym
            wrongLettersPtr->setString(wrong_letters);
            wrongLettersPtr->setFillColor(sf::Color(255, 165, 0)); // Orange
        } else {
            wrongLettersPtr->setString("Wrong: " + wrong_letters);
            wrongLettersPtr->setFillColor(sf::Color::Red); // Czerwony dla błędnych liter
        }

    }
    
    // Przywróć normalny prompt jeśli gra już się zaczęła
    if (promptPtr && mode == 1 && wrong_letters.find("Waiting") == std::string::npos) {
        promptPtr->setString("Guess a letter:");
        promptPtr->setFillColor(sf::Color::Black);
        centerText(promptPtr, 800 / 2.f, 200);

    }
}

void handleLives(const std::string& line) {
    std::istringstream iss(line);
    std::string token;
    int lives = 0, points = 0, winpts = 0;
    iss >> token >> lives >> token >> points >> token >> winpts;

    if (livesDisplayPtr) livesDisplayPtr->setString("Lives: " + std::to_string(lives));
    if (pointsDisplayPtr) pointsDisplayPtr->setString("Points: " + std::to_string(points));
    if (winPointsDisplayPtr) winPointsDisplayPtr->setString("Win: " + std::to_string(winpts));
}

void handleGameOver(const std::string& line) {
    mode = 3;
    roundEndTimerActive = false;
    
    // Wyczyść listę graczy - będzie używana do rankingu
    if (playersListPtr) {
        playersListPtr->setString("");
    }
    
    if (wordDisplayPtr) {
        wordDisplayPtr->setString("*** GAME OVER ***");
        wordDisplayPtr->setFillColor(sf::Color(255, 215, 0)); // gold
        centerText(wordDisplayPtr, 800 / 2.f, 150);
    }
    
    if (promptPtr) {
        promptPtr->setString("Game finished! Check final rankings →");
        promptPtr->setFillColor(sf::Color(255, 215, 0));
        centerText(promptPtr, 800 / 2.f, 200);
    }
    
}

void handleWin(const std::string& line) {
    if (mode == 3 || mode == 4) return; // ignore during GAME_OVER

    std::istringstream iss(line);
    std::string token, word;
    iss >> token >> word; // "WIN <word>"

    mode = 2;
    roundEndTimerActive = true;
    roundEndClock.restart();
    lastDisplayedSeconds = 10;
    isWinRound = true;

    if (wordDisplayPtr) {
        std::string spaced_word;
        for (size_t i = 0; i < word.size(); i++) {
            spaced_word += word[i];
            if (i < word.size() - 1) spaced_word += " ";
        }
        wordDisplayPtr->setString(spaced_word);
        wordDisplayPtr->setFillColor(sf::Color::Green);
        centerText(wordDisplayPtr, 800 / 2.f, 150);
    }

    if (promptPtr) {
        promptPtr->setString("ROUND WON! New round in 10 seconds...");
        promptPtr->setFillColor(sf::Color::Green);
        centerText(promptPtr, 800 / 2.f, 200);
    }
}

void handleAllLost(const std::string& line) {
    std::istringstream iss(line);
    std::string token, secret_word;
    iss >> token >> secret_word; // "ALL_LOST <secret>"

    mode = 2;
    roundEndTimerActive = true;
    roundEndClock.restart();
    lastDisplayedSeconds = 10;
    isWinRound = false;

    if (wordDisplayPtr) {
        std::string spaced_word;
        for (size_t i = 0; i < secret_word.size(); i++) {
            spaced_word += secret_word[i];
            if (i < secret_word.size() - 1) spaced_word += " ";
        }
        wordDisplayPtr->setString(spaced_word);
        wordDisplayPtr->setFillColor(sf::Color::Red);
        centerText(wordDisplayPtr, 800 / 2.f, 150);
    }

    if (promptPtr) {
        promptPtr->setString("ALL LOST! Secret was: " + secret_word + ". Next round in 10s...");
        promptPtr->setFillColor(sf::Color::Red);
        centerText(promptPtr, 800 / 2.f, 200);
    }
}

void handleWaitRematch(const std::string& line) {
    std::string time_str = line.substr(13);
    time_str.erase(0, time_str.find_first_not_of(" \t\r\n"));
    time_str.erase(time_str.find_last_not_of(" \t\r\n") + 1);

    remainingWaitTime = std::stoi(time_str);
    countdownClock.restart();

}

void handleRematchOk(const std::string& line) {
    printf("REMATCH_OK received - resetting to nickname input\n");
    
    //full_reset_client();
    roundEndTimerActive = false;
    rematchRequested = false;
    roundTimerClock.restart();
    //mode = 0;
    
    if (promptPtr) {
        promptPtr->setString("Enter your nickname below:");
        promptPtr->setFillColor(sf::Color::Black);
        centerText(promptPtr, 800 / 2.f, 200);
    }
    
    if (playersListPtr) {
        playersListPtr->setString("");
    }
    
    if (wordDisplayPtr) {
        wordDisplayPtr->setString("");
    }
    mode = 1;
}

void handlePlayersList(const std::string& line) {
    // Jeśli jesteśmy w trybie GAME_OVER (mode 3), nie nadpisuj rankingu
    if (mode == 3) return;
    
    std::string data = line.substr(13);
    std::string display = "=== PLAYERS ===\n";

    std::istringstream iss(data);
    std::string player_data;

    while (std::getline(iss, player_data, ',')) {
        if (player_data.empty()) continue;

        std::istringstream player_iss(player_data);
        std::string nick, lives_str, points_str, winpts_str;

        std::getline(player_iss, nick, ':');
        std::getline(player_iss, lives_str, ':');
        std::getline(player_iss, points_str, ':');
        std::getline(player_iss, winpts_str, ':');

        display += nick + "\n";
        display += "  <3 " + lives_str + " | " + points_str + "pts | W:" + winpts_str + "\n";
    }

    if (playersListPtr) {
        playersListPtr->setString(display);
    }
}

void handleAlreadyGuessed() {
    if (promptPtr && mode == 1) {
        promptPtr->setString("Already guessed! Try another letter:");
        promptPtr->setFillColor(sf::Color(255, 165, 0)); // Orange
        centerText(promptPtr, 800 / 2.f, 200);
    }
}

void handleYouLost() {
    if (promptPtr) {
        promptPtr->setString("You lost! Wait for next round...");
        promptPtr->setFillColor(sf::Color::Red);
        centerText(promptPtr, 800 / 2.f, 200);
    }
}

void handleWaitingForPlayers(const std::string& line) {
    mode = 4; // New mode for waiting
    
    if (promptPtr) {
        promptPtr->setString(line);
        promptPtr->setFillColor(sf::Color(255, 165, 0)); // Orange
        centerText(promptPtr, 800 / 2.f, 200);
    }
    
    // Clear game elements
    if (wordDisplayPtr) wordDisplayPtr->setString("");
    if (wrongLettersPtr) wrongLettersPtr->setString("");
    
}


void poll_handler(int &my_fd) {
    struct pollfd pfd { my_fd, POLLIN, 0 };
    if (poll(&pfd, 1, 0) <= 0) return;

    if (pfd.revents & POLLIN) {
        char buffer[512];
        ssize_t received = recv(my_fd, buffer, sizeof(buffer) - 1, 0);
        if (received <= 0) return;

        buffer[received] = '\0';
        std::string msg = leftover + std::string(buffer);

        size_t last_newline = msg.find_last_of('\n');
        if (last_newline != std::string::npos) {
            leftover = msg.substr(last_newline + 1);
            msg = msg.substr(0, last_newline + 1);
        } else {
            leftover = msg;
            return;
        }

        std::istringstream ss(msg);
        std::string line;
        while (std::getline(ss, line)) {
            printf("[SERVER] %s\n", line.c_str());
            if (line.empty()) continue;

            // --- PREPROCESS: usuń prefiksy, które serwer czasem dokleja ---
            std::string proc = line;
            const std::string BCAST = "BROADCAST ";
            const std::string PERS  = "PERSONAL ";
            const std::string PERS_TO = "PERSONAL_TO_";

            if (proc.rfind(BCAST, 0) == 0) {
                proc = proc.substr(BCAST.size());
            } else if (proc.rfind(PERS, 0) == 0) {
                proc = proc.substr(PERS.size());
            } else if (proc.rfind(PERS_TO, 0) == 0) {
                // PERSONAL_TO_<fd> <rest...>
                size_t sp = proc.find(' ');
                if (sp != std::string::npos) proc = proc.substr(sp + 1);
            }

            // Teraz parsuj przetworzoną linię (proc)
            switch(parseMessageType(proc)) {
                case ServerMsgType::NickOk: handleNickOk(); break;
                case ServerMsgType::NickTaken: handleNickTaken(); break;
                case ServerMsgType::Word: handleWord(proc); break;
                case ServerMsgType::Lives: handleLives(proc); break;
                case ServerMsgType::GameOver: handleGameOver(proc); break;
                case ServerMsgType::Win: handleWin(proc); break;
                case ServerMsgType::AllLost: handleAllLost(proc); break;
                case ServerMsgType::WaitRematch: handleWaitRematch(proc); break;
                case ServerMsgType::RematchOk: handleRematchOk(proc); break;
                case ServerMsgType::PlayersList: handlePlayersList(proc); break;
                case ServerMsgType::AlreadyGuessed: handleAlreadyGuessed(); break;
                case ServerMsgType::YouLost: handleYouLost(); break;
                case ServerMsgType::RoundTime: handleRoundTime(proc); break;
                case ServerMsgType::WaitingForPlayers: handleWaitingForPlayers(proc); break;
                case ServerMsgType::NewRound: handleNewRound(); break;                    
                case ServerMsgType::WarmupFinished: handleWarmupFinished(); break;        
                case ServerMsgType::WarmupCountdown: handleWarmupCountdown(proc); break;
                case ServerMsgType::Connected: handleConnected(proc); break;
                case ServerMsgType::PlayerJoined: handlePlayerJoined(proc); break;
                default: break;
            }
        }
    }
}

int main(int argc, char** argv) {

    signal(SIGPIPE, SIG_IGN);

    // Default IP
    std::string server_ip = "127.0.0.1";
    
    // Check command line args
    if (argc > 1) {
        server_ip = argv[1];
    } else {
        printf("Usage: %s <IP_ADDRESS>\nDefaulting to 127.0.0.1\n", argv[0]);
    }

    // ... SFML Initialization ...

    // Use the parsed IP
    int my_fd = net_connection(server_ip.c_str(), 1234);

    // ADD THIS CHECK:
    if (my_fd < 0) {
        printf("CRITICAL ERROR: Could not connect to server %s.\n", server_ip.c_str());
        printf("Check if Server is running and IP is correct.\n");
        return 1; // Exit the program immediately, do not open window
    }

    sf::RenderWindow window(sf::VideoMode(800, 600), "Wisielec Online");
    window.setFramerateLimit(60);

    sf::Font font;
    if (!font.loadFromFile("src/arial.ttf")) return 1;

    // Title text 
    sf::Text title;
    title.setFont(font);
    title.setString("Wisielec Online");
    title.setCharacterSize(48);
    title.setFillColor(sf::Color::Black);
    title.setStyle(sf::Text::Bold);
    sf::FloatRect titleRect = title.getLocalBounds();
    title.setOrigin(titleRect.left + titleRect.width / 2.0f, 0);
    title.setPosition(800 / 2.0f, 20);

    // Word display (the masked password)
    sf::Text wordDisplay;
    wordDisplay.setFont(font);
    wordDisplay.setString("");
    wordDisplay.setCharacterSize(48);
    wordDisplay.setFillColor(sf::Color::Black);
    wordDisplay.setStyle(sf::Text::Bold);
    wordDisplayPtr = &wordDisplay;

    // Lives display (bottom left)
    sf::Text livesDisplay;
    livesDisplay.setFont(font);
    livesDisplay.setString("Lives: 5");
    livesDisplay.setCharacterSize(24);
    livesDisplay.setFillColor(sf::Color::Black);
    livesDisplay.setPosition(50, 500);
    livesDisplayPtr = &livesDisplay;

    // Points display (bottom right)
    sf::Text pointsDisplay;
    pointsDisplay.setFont(font);
    pointsDisplay.setString("Points: 0");
    pointsDisplay.setCharacterSize(24);
    pointsDisplay.setFillColor(sf::Color::Black);
    pointsDisplay.setPosition(650, 500);
    pointsDisplayPtr = &pointsDisplay;

    // Win points display
    sf::Text winPointsDisplay;
    winPointsDisplay.setFont(font);
    winPointsDisplay.setString("Win: 0");
    winPointsDisplay.setCharacterSize(24);
    winPointsDisplay.setFillColor(sf::Color::Blue);
    winPointsDisplay.setPosition(650, 530);
    winPointsDisplayPtr = &winPointsDisplay;

    // Wrong letters display
    sf::Text wrongLetters;
    wrongLetters.setFont(font);
    wrongLetters.setString("Wrong: ");
    wrongLetters.setCharacterSize(28);
    wrongLetters.setFillColor(sf::Color::Red);
    wrongLetters.setPosition(50, 400);
    wrongLettersPtr = &wrongLetters;

    // Prompt text
    sf::Text prompt;
    prompt.setFont(font);
    prompt.setString("Enter your nickname below:");
    prompt.setCharacterSize(36);
    prompt.setFillColor(sf::Color::Black);
    promptPtr = &prompt;
    sf::FloatRect promptRect = prompt.getLocalBounds();
    prompt.setOrigin(promptRect.left + promptRect.width / 2.0f, promptRect.top + promptRect.height / 2.0f);
    prompt.setPosition(800 / 2.0f, 250);

    // Input field
    sf::String userInput;
    sf::Text inputText;
    inputText.setFont(font);
    inputText.setCharacterSize(30);
    inputText.setFillColor(sf::Color::Black);

    // Input field draw
    sf::RectangleShape inputBox(sf::Vector2f(400, 50));
    inputBox.setFillColor(sf::Color::White);
    inputBox.setOutlineColor(sf::Color::Black);
    inputBox.setOutlineThickness(2);
    inputBox.setPosition(800 / 2.0f - 200, 320);

    //Players list
    sf::Text playersList;
    playersList.setFont(font);
    playersList.setString("");
    playersList.setCharacterSize(15);
    playersList.setFillColor(sf::Color::Black);
    playersList.setPosition(600, 20);
    playersListPtr = &playersList;

    sf::Text inactivityTimer;
    inactivityTimer.setFont(font);
    inactivityTimer.setString("Inactivity: 60s");
    inactivityTimer.setCharacterSize(20);
    inactivityTimer.setFillColor(sf::Color::Black);
    inactivityTimer.setPosition(10, 10);
    inactivityTimerPtr = &inactivityTimer;

    sf::Text roundTimer;
    roundTimer.setFont(font);
    roundTimer.setString("Round: 60s");
    roundTimer.setCharacterSize(20);
    roundTimer.setFillColor(sf::Color::Black);
    roundTimer.setPosition(10, 35);
    roundTimerPtr = &roundTimer;

    inactivityClock.restart();

    // Main window loop
    while (window.isOpen()) {

            // LOKALNY UPDATE TIMERA RUNDY
        if (mode == 1 || mode == 2) {
            int elapsed = static_cast<int>(roundTimerClock.getElapsedTime().asSeconds());
            int remaining = lastServerRoundTime - elapsed;
            if (remaining < 0) remaining = 0;
            
            if (roundTimerPtr) {
                if (remaining <= 10) {
                    roundTimerPtr->setFillColor(sf::Color::Red);
                } else if (remaining <= 20) {
                    roundTimerPtr->setFillColor(sf::Color(255, 165, 0));
                } else {
                    roundTimerPtr->setFillColor(sf::Color::Black);
                }
                roundTimerPtr->setString("Round: " + std::to_string(remaining) + "s");
            }
        }

        if (my_fd >= 0) poll_handler(my_fd);

        // Check if timer is active and update countdown display
        if (roundEndTimerActive) {
            float elapsed = roundEndClock.getElapsedTime().asSeconds();
            int remainingSeconds = lastDisplayedSeconds - static_cast<int>(elapsed);
            
            if (remainingSeconds < 0) remainingSeconds = 0;
            static int previousSeconds = -1;
            
            if (remainingSeconds != previousSeconds) {
                previousSeconds = remainingSeconds; 
                if (promptPtr) {
                    std::string message;
                    
                    // Sprawdź czy to GAME_OVER (po gwiazdkach w wordDisplay)
                    if (wordDisplayPtr && wordDisplayPtr->getString().toAnsiString().find("***") != std::string::npos) {
                        message = "GAME OVER! New game in " + std::to_string(remainingSeconds) + " seconds...";
                        promptPtr->setFillColor(sf::Color(255, 215, 0)); // Gold
                    } else {
                        message = isWinRound ? "ROUND WON! New round in " : "ALL PLAYERS LOST! New round in ";
                        message += std::to_string(remainingSeconds) + " seconds...";
                    }
                    
                    promptPtr->setString(message);
                    sf::FloatRect r = promptPtr->getLocalBounds();
                    promptPtr->setOrigin(r.left + r.width / 2.f,
                                        r.top + r.height / 2.f);
                    promptPtr->setPosition(800 / 2.f, 200);
                }
            }
            
            // Check if timer finished
            if (elapsed >= lastDisplayedSeconds) {
                roundEndTimerActive = false;
                mode = 1;
                reset_game_display();
                printf("Timer completed - display reset\n");
            }
        }

        sf::Event event;
        bool inputChanged = false;

        while (window.pollEvent(event)) {
            // Close the client
            if (event.type == sf::Event::Closed)
                window.close();
                
            if (mode == 3) {
                if (event.type == sf::Event::KeyPressed) {
                    if (event.key.code == sf::Keyboard::R) {
                        rematchRequested = true;
                        
                        std::string msg = "REMATCH\n";
                        send(my_fd, msg.c_str(), msg.size(), 0);
                        inactivityClock.restart();
                        
                        // Immediate visual feedback
                        if (promptPtr) {
                            promptPtr->setFillColor(sf::Color::Green);
                            promptPtr->setString("Restart request sent...");
                        }
                    }
                    if (event.key.code == sf::Keyboard::Q) {
                        window.close();
                    }
                }
            }
            
            if (event.type == sf::Event::TextEntered) {
                // Only allow input in modes 0 (nickname) and 1 (game)
                if (mode == 0 || mode == 1 || mode == 4) {
                    inputChanged = true;
                    if (event.text.unicode == 8) { //8 is backspace
                        if (!userInput.isEmpty())
                            userInput.erase(userInput.getSize() - 1, 1);

                    } else if (event.text.unicode == 13) { //13 is enter
                        std::string input = userInput.toAnsiString();

                        if (mode == 0 || mode == 4) { // sending nickname
                            printf("Nickname entered: %s\n", input.c_str());
                            std::string message = "NICK " + input + "\n";

                            if (my_fd >= 0) {
                                send(my_fd, message.c_str(), message.size(), 0);
                            } else {
                                printf("Error sending nickname: %s\n", strerror(errno));
                            }
                            userInput.clear();

                        } else if (mode == 1) { // sending guesses
                            printf("Guess entered: %s\n", input.c_str());
                            std::string message = "GUESS " + input + "\n";
                            if (my_fd >= 0) {
                                send(my_fd, message.c_str(), message.size(), 0);
                                inactivityClock.restart();
                            }
                            userInput.clear();
                        }
                    } else if (event.text.unicode < 128) {
                        if (mode == 1) { // guessing
                            if (userInput.getSize() < 1) {
                                userInput += static_cast<char>(event.text.unicode);
                            }
                        } else {
                            if (userInput.getSize() < 20) {
                                userInput += static_cast<char>(event.text.unicode);
                            }
                        }
                    }
                }
            }
        }

        if (inputChanged) {
            inputText.setString(userInput);
            inputText.setPosition(inputBox.getPosition().x + 10, 
                                  inputBox.getPosition().y + inputBox.getSize().y / 2.0f - inputText.getCharacterSize() / 2.0f);
        }

        // Simple cursor (only show in active input modes)
        sf::Text displayText = inputText;
        if (mode == 0 || mode == 1 || mode == 4) {
            displayText.setString(userInput + "_");
        } else {
            displayText.setString(userInput);
        }

        if (inactivityTimerPtr && my_fd >= 0) {
            int remainingInactivity = 300 - static_cast<int>(inactivityClock.getElapsedTime().asSeconds());
            if (remainingInactivity < 0) remainingInactivity = 0;
            
            if (remainingInactivity <= 0) {
                printf("Inactivity timeout. Closing window...\n");
                if (my_fd >= 0) {
                    close(my_fd);
                }
                window.close();
            } else {
                if (remainingInactivity <= 10) {
                    inactivityTimerPtr->setFillColor(sf::Color::Red);
                } else if (remainingInactivity <= 30) {
                    inactivityTimerPtr->setFillColor(sf::Color(255, 165, 0));
                } else {
                    inactivityTimerPtr->setFillColor(sf::Color::Black);
                }
            }
            inactivityTimerPtr->setString("Inactivity: " + std::to_string(remainingInactivity) + "s");
        }

        window.clear(sf::Color(200, 200, 200));
        window.draw(title);
        if (mode == 4) {
            // LOBBY MODE - pokaż tylko listę graczy i prompt
            window.draw(prompt);
            window.draw(playersList);
        }
        // Only draw word, lives, points, and wrong letters in game mode (1) or waiting mode (2)
        if (mode == 1 || mode == 2) {
            window.draw(wordDisplay);
            window.draw(livesDisplay);
            window.draw(pointsDisplay);
            window.draw(winPointsDisplay);
            window.draw(wrongLetters);
            window.draw(playersList);
        } 

        if (mode == 3) {
            if (countdownClock.getElapsedTime().asSeconds() >= 1.0f) {
                if (remainingWaitTime > 0) {
                    remainingWaitTime--;
                }
                countdownClock.restart();
            }
            
            if (remainingWaitTime <= 0 && !rematchRequested) {
                printf("Time's up! No rematch requested. Closing...\n");
                if (my_fd >= 0) {
                    close(my_fd);
                }
                window.close();
                break;
            }

            prompt.setString("Press [R] to play again, Wait " + 
                            std::to_string(remainingWaitTime) + "s" +
                            "\nPress [Q] to quit");
            
            
            window.draw(wordDisplay); // "*** GAME OVER ***"
            window.draw(prompt);
            window.draw(playersList); // Finalny ranking
        }
        else {
            window.draw(prompt);
        }
        
        // Only show input box in active modes
        if (mode == 0 || mode == 1 || mode == 4) { // DODAJ mode 4!
            window.draw(inputBox);
            window.draw(displayText);
        }

        if (my_fd >= 0 && inactivityTimerPtr) {
            window.draw(inactivityTimer);
        }
        if (my_fd >= 0 && roundTimerPtr && (mode == 1 || mode == 2)) {
            window.draw(roundTimer);
        }
        
        window.display();

        
    }

    if (my_fd >= 0) close(my_fd);
    return 0;
}