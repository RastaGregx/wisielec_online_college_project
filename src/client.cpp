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
                printf("Server: %s\n", buffer);
            } else if (received == 0) {
                printf("Server closed the connection.\n");
                close(my_fd);
                my_fd = -1;
            } else {
                if (errno != EWOULDBLOCK && errno != EAGAIN) {
                    printf("Receive error: %s\n", strerror(errno));
                }
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

    // Prompt text
    sf::Text prompt;
    prompt.setFont(font);
    prompt.setString("Enter your nickname below:");
    prompt.setCharacterSize(36);
    prompt.setFillColor(sf::Color::Black);
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

    int my_fd = net_connection();

    // the nickname mode
    int mode = 0 ;

    // Main window loop
    while (window.isOpen()) {

        sf::Event event;

        while (window.pollEvent(event)) {
            // Close the client
            if (event.type == sf::Event::Closed)
                window.close();

            if (event.type == sf::Event::TextEntered) {
                if (event.text.unicode == 8) { //8 is backspace
                    // crash prevention - deleting nothing
                    if (!userInput.isEmpty())
                        userInput.erase(userInput.getSize() - 1, 1);

                } else if (event.text.unicode == 13) { //13 is enter
                    std::string input = userInput.toAnsiString(); //converting sfml string to c

                    if (mode == 0) { // sending nickname

                        printf("Nickname entered: %s\n", input.c_str());
                        std::string message = "NICK " + input + "\n";

                        if (my_fd >= 0) {
                            send(my_fd, message.c_str(), message.size(), 0);
                        } else {
                            printf("Error sending nickname: %s\n", strerror(errno));
                        }
                        
                        //mode change
                        userInput.clear();
                        mode = 1;
                        prompt.setString("Guess the password:");
                        sf::FloatRect promptRect = prompt.getLocalBounds();
                        prompt.setOrigin(promptRect.left + promptRect.width / 2.0f,
                                        promptRect.top + promptRect.height / 2.0f);
                        prompt.setPosition(800 / 2.0f, 250);

                    } else if (mode == 1) { // sending  guesses
                        printf("Guess entered: %s\n", input.c_str());
                        std::string message = "GUESS " + input + "\n";
                        if (my_fd >= 0) send(my_fd, message.c_str(), message.size(), 0);

                        userInput.clear();
                    }
                } else if (event.text.unicode < 128) { //input limit to ascii and 20 char only
                    if (userInput.getSize() < 20) 
                        userInput += static_cast<char>(event.text.unicode);
                }
            }
    
        }

        inputText.setString(userInput);

        //centering
        float verticalCenter = inputText.getCharacterSize() / 2.0f;
        inputText.setOrigin(0, verticalCenter); 
        inputText.setPosition(inputBox.getPosition().x + 10, inputBox.getPosition().y + inputBox.getSize().y / 2.0f);

        //simple cursor
        sf::Text displayText = inputText;
        displayText.setString(userInput + "_");

        window.clear(sf::Color(200, 200, 200));
        window.draw(title);
        window.draw(prompt);
        window.draw(inputBox);
        window.draw(displayText);
        window.display();

        if (my_fd >= 0) poll_handler(my_fd);
    }

    if (my_fd >= 0) close(my_fd);
    return 0;
}
