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

int net_connection() {
    sockaddr_in client_data;
    client_data.sin_addr.s_addr = inet_addr("127.0.0.1");
    client_data.sin_family = AF_INET;
    client_data.sin_port = htons(1234);

    int my_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (my_fd < 0) {
        printf("Error creating socket: %s\n", strerror(errno));
        return -1;
    }

    if (connect(my_fd, (struct sockaddr*)&client_data, sizeof(client_data)) < 0) {
        printf("Could not connect: %s\n", strerror(errno));
        close(my_fd);
        return -1;
    }

    int fd_flags = fcntl(my_fd, F_GETFL);
    if (fd_flags < 0) {
        printf("Failed to get flags: %s\n", strerror(errno));
        return -1;
    }
    fcntl(my_fd, F_SETFL, fd_flags | O_NONBLOCK);

    return my_fd;
}

int mode = 0; // 0 = Inputting Nick, 1 = Game, 2 = Round End Waiting
sf::Text* promptPtr = nullptr;
sf::Text* wordDisplayPtr = nullptr;
sf::Text* livesDisplayPtr = nullptr;
sf::Text* pointsDisplayPtr = nullptr;
sf::Text* wrongLettersPtr = nullptr;
sf::Text* winPointsDisplayPtr = nullptr;
sf::Text* playersListPtr = nullptr;

sf::Clock roundEndClock;
bool roundEndTimerActive = false;
int lastDisplayedSeconds = 10;
bool isWinRound = false;

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

void poll_handler(int &my_fd) {
    struct pollfd pfd;
    pfd.fd = my_fd;
    pfd.events = POLLIN;
    pfd.revents = 0;

    int ret = poll(&pfd, 1, 0);
    if (ret > 0) {
        if (pfd.revents & POLLIN) {
            char buffer[512];
            ssize_t received = recv(my_fd, buffer, sizeof(buffer) - 1, 0);

            if (received > 0) {
                buffer[received] = '\0';
                std::string msg(buffer);

                printf("Server: %s\n", msg.c_str());
                
                // Split message by newlines and process each line
                std::istringstream msg_stream(msg);
                std::string line;
                
                while (std::getline(msg_stream, line)) {
                    if (line.empty()) continue;
                    
                    // Nick ok
                    if (line.find("NICK_OK") != std::string::npos) {
                        mode = 1;
                        if (promptPtr) {
                            promptPtr->setString("Guess a letter:");
                            promptPtr->setFillColor(sf::Color::Black);
                            sf::FloatRect r = promptPtr->getLocalBounds();
                            promptPtr->setOrigin(r.left + r.width / 2.f,
                                                r.top + r.height / 2.f);
                            promptPtr->setPosition(800 / 2.f, 200);
                        }
                    }

                    // Nick taken
                    else if (line.find("NICK_TAKEN") != std::string::npos) {
                        mode = 0;
                        if (promptPtr) {
                            promptPtr->setString("Nickname taken! Enter another:");
                            sf::FloatRect r = promptPtr->getLocalBounds();
                            promptPtr->setOrigin(r.left + r.width / 2.f,
                                                r.top + r.height / 2.f);
                            promptPtr->setPosition(800 / 2.f, 250);
                        }
                    }

                    // Parse WORD response (broadcast to all players)
                    else if (line.find("WORD ") != std::string::npos) {
                        std::istringstream iss(line);
                        std::string token, word;
                        
                        iss >> token; // "WORD"
                        iss >> word;  // the masked word
                        
                        // Check for WRONG letters
                        std::string wrong_letters;
                        iss >> token; // "WRONG" or something else
                        if (token == "WRONG") {
                            std::string letter;
                            while (iss >> letter) {
                                // Stop if we hit LIVES or POINTS (shouldn't happen but just in case)
                                if (letter == "LIVES" || letter == "POINTS") break;
                                wrong_letters += letter + " ";
                            }
                            // Remove trailing space
                            if (!wrong_letters.empty() && wrong_letters.back() == ' ') {
                                wrong_letters.pop_back();
                            }
                        }

                        if (wordDisplayPtr) {
                            // Add spaces between letters for better visibility
                            std::string spaced_word;
                            for (size_t i = 0; i < word.length(); i++) {
                                spaced_word += word[i];
                                if (i < word.length() - 1) spaced_word += " ";
                            }
                            wordDisplayPtr->setString(spaced_word);
                            wordDisplayPtr->setFillColor(sf::Color::Black);
                            sf::FloatRect r = wordDisplayPtr->getLocalBounds();
                            wordDisplayPtr->setOrigin(r.left + r.width / 2.f,
                                                     r.top + r.height / 2.f);
                            wordDisplayPtr->setPosition(800 / 2.f, 150);
                        }

                        if (wrongLettersPtr) {
                            if (!wrong_letters.empty()) {
                                wrongLettersPtr->setString("Wrong: " + wrong_letters);
                            } else {
                                wrongLettersPtr->setString("Wrong: ");
                            }
                        }
                    }

                    // Parse LIVES and POINTS (personal updates)
                    else if (line.find("LIVES ") != std::string::npos) {
                        printf("DEBUG: Parsing LIVES line: %s\n", line.c_str());
                        std::istringstream iss(line);
                        std::string token;
                        int lives = 0, points = 0, winpts = 0;  
                        
                        iss >> token; // "LIVES"
                        iss >> lives;
                        iss >> token; // "POINTS"
                        if (token == "POINTS") {
                            iss >> points;
                        }
                        iss >> token; // "WINPTS" 
                        if (token == "WINPTS") {  
                            iss >> winpts;         
                        }                          

                        printf("DEBUG: Setting lives=%d, points=%d, winpts=%d\n", lives, points, winpts);

                        if (livesDisplayPtr) {
                            livesDisplayPtr->setString("Lives: " + std::to_string(lives));
                        }

                        if (pointsDisplayPtr) {
                            pointsDisplayPtr->setString("Points: " + std::to_string(points));
                        }
                        
                        if (winPointsDisplayPtr) {
                            winPointsDisplayPtr->setString("Win: " + std::to_string(winpts));
                        }
                    }

                    else if (line.find("PLAYERS_LIST ") != std::string::npos) {
                        std::string data = line.substr(13);
                        
                        // Format: nick:lives:points:winpts,nick:lives:points:winpts,...
                        std::string display = "=== PLAYERS ===\n";
                        
                        std::istringstream iss(data);
                        std::string player_data;
                        
                        while (std::getline(iss, player_data, ',')) {
                            if (player_data.empty()) continue;
                            
                            // Parse nick:lives:points:winpts
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

                    // Win condition
                    else if (line.find("WIN ") != std::string::npos) {
                        std::istringstream iss(line);
                        std::string token, word;
                        
                        iss >> token; // "WIN"
                        iss >> word;  // the complete word

                        mode = 2;
                        roundEndTimerActive = true;
                        roundEndClock.restart();
                        lastDisplayedSeconds = 10;
                        isWinRound = true;

                        if (wordDisplayPtr) {
                            // Add spaces between letters
                            std::string spaced_word;
                            for (size_t i = 0; i < word.length(); i++) {
                                spaced_word += word[i];
                                if (i < word.length() - 1) spaced_word += " ";
                            }
                            wordDisplayPtr->setString(spaced_word);
                            wordDisplayPtr->setFillColor(sf::Color::Green);
                            sf::FloatRect r = wordDisplayPtr->getLocalBounds();
                            wordDisplayPtr->setOrigin(r.left + r.width / 2.f,
                                                     r.top + r.height / 2.f);
                            wordDisplayPtr->setPosition(800 / 2.f, 150);
                        }

                        if (promptPtr) {
                            promptPtr->setString("ROUND WON! New round in 10 seconds...");
                            promptPtr->setFillColor(sf::Color::Green);
                            sf::FloatRect r = promptPtr->getLocalBounds();
                            promptPtr->setOrigin(r.left + r.width / 2.f,
                                                r.top + r.height / 2.f);
                            promptPtr->setPosition(800 / 2.f, 200);
                        }
                        printf("Round won - starting 10 second countdown\n");
                    }

                    // All lost
                    else if (line.find("ALL_LOST") != std::string::npos) {
                        mode = 2;
                        roundEndTimerActive = true;
                        roundEndClock.restart();
                        lastDisplayedSeconds = 10;
                        isWinRound = false;

                        if (promptPtr) {
                            promptPtr->setString("ALL PLAYERS LOST! New round in 10 seconds...");
                            promptPtr->setFillColor(sf::Color::Red);
                            sf::FloatRect r = promptPtr->getLocalBounds();
                            promptPtr->setOrigin(r.left + r.width / 2.f,
                                                r.top + r.height / 2.f);
                            promptPtr->setPosition(800 / 2.f, 200);
                        }
                        printf("All players lost - starting 10 second countdown\n");
                    }

                    // New round
                    else if (line.find("NEW_ROUND") != std::string::npos) {
                        printf("NEW_ROUND signal received from server\n");
                    }

                    // Already guessed
                    else if (line.find("ALREADY_GUESSED") != std::string::npos) {
                        if (promptPtr && mode == 1) {
                            promptPtr->setString("Already guessed! Try another letter:");
                            promptPtr->setFillColor(sf::Color(255, 165, 0));
                            sf::FloatRect r = promptPtr->getLocalBounds();
                            promptPtr->setOrigin(r.left + r.width / 2.f,
                                                r.top + r.height / 2.f);
                            promptPtr->setPosition(800 / 2.f, 200);
                        }
                    }

                    // Player lost
                    else if (line.find("YOU_LOST") != std::string::npos || 
                             line.find("LOSE") != std::string::npos ||
                             line.find("DEAD") != std::string::npos) {
                        if (promptPtr) {
                            promptPtr->setString("You lost! Wait for next round...");
                            promptPtr->setFillColor(sf::Color::Red);
                            sf::FloatRect r = promptPtr->getLocalBounds();
                            promptPtr->setOrigin(r.left + r.width / 2.f,
                                                r.top + r.height / 2.f);
                            promptPtr->setPosition(800 / 2.f, 200);
                        }
                    }
                }

            } else if (received == 0) {
                printf("Server closed connection\n");
                close(my_fd);
                my_fd = -1;
            }
        }
    }
}
        
int main() {
    sf::RenderWindow window(sf::VideoMode(800, 600), "Wisielec Online");
    window.setFramerateLimit(60);

    sf::Font font;
    if (!font.loadFromFile("arial.ttf")) return 1;

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

    // Lives display (bottom left) - YOUR EXISTING ONE
    sf::Text livesDisplay;
    livesDisplay.setFont(font);
    livesDisplay.setString("Lives: 5");
    livesDisplay.setCharacterSize(24);
    livesDisplay.setFillColor(sf::Color::Black);
    livesDisplay.setPosition(50, 500);
    livesDisplayPtr = &livesDisplay;

    // Points display (bottom right) - YOUR EXISTING ONE
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
    playersList.setCharacterSize(18);
    playersList.setFillColor(sf::Color::Black);
    playersList.setPosition(620, 20);
    playersListPtr = &playersList;

    int my_fd = net_connection();

    // Main window loop
    while (window.isOpen()) {

        // Check if timer is active and update countdown display
        if (roundEndTimerActive) {
            float elapsed = roundEndClock.getElapsedTime().asSeconds();
            int remainingSeconds = 10 - static_cast<int>(elapsed);
            
            if (remainingSeconds < 0) remainingSeconds = 0;
            
            // Update display if the second changed
            if (remainingSeconds != lastDisplayedSeconds) {
                lastDisplayedSeconds = remainingSeconds;
                
                if (promptPtr) {
                    std::string message = isWinRound ? "ROUND WON! New round in " : "ALL PLAYERS LOST! New round in ";
                    message += std::to_string(remainingSeconds) + " seconds...";
                    promptPtr->setString(message);
                    
                    sf::FloatRect r = promptPtr->getLocalBounds();
                    promptPtr->setOrigin(r.left + r.width / 2.f,
                                        r.top + r.height / 2.f);
                    promptPtr->setPosition(800 / 2.f, 200);
                }
            }
            
            // Check if 10 seconds have passed
            if (elapsed >= 10.0f) {
                roundEndTimerActive = false;
                mode = 1;
                reset_game_display();
                printf("Round timer completed - display reset\n");
            }
        }

        sf::Event event;
        bool inputChanged = false;

        while (window.pollEvent(event)) {
            // Close the client
            if (event.type == sf::Event::Closed)
                window.close();

            if (event.type == sf::Event::TextEntered) {
                // Only allow input in modes 0 (nickname) and 1 (game)
                if (mode == 0 || mode == 1) {
                    inputChanged = true;
                    if (event.text.unicode == 8) { //8 is backspace
                        if (!userInput.isEmpty())
                            userInput.erase(userInput.getSize() - 1, 1);

                    } else if (event.text.unicode == 13) { //13 is enter
                        std::string input = userInput.toAnsiString();

                        if (mode == 0) { // sending nickname
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
                            if (my_fd >= 0) send(my_fd, message.c_str(), message.size(), 0);

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
        if (mode == 0 || mode == 1) {
            displayText.setString(userInput + "_");
        } else {
            displayText.setString(userInput);
        }

        window.clear(sf::Color(200, 200, 200));
        window.draw(title);
        
        // Only draw word, lives, points, and wrong letters in game mode (1) or waiting mode (2)
        if (mode == 1 || mode == 2) {
            window.draw(wordDisplay);
            window.draw(livesDisplay);
            window.draw(pointsDisplay);
            window.draw(winPointsDisplay);
            window.draw(wrongLetters);
            window.draw(playersList);
        }
                
        window.draw(prompt);
        
        // Only show input box in active modes
        if (mode == 0 || mode == 1) {
            window.draw(inputBox);
            window.draw(displayText);
        }
        
        window.display();

        if (my_fd >= 0) poll_handler(my_fd);
    }

    if (my_fd >= 0) close(my_fd);
    return 0;
}