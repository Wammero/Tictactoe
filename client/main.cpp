#include <iostream>
#include <string>
#include <map>
#include <functional>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <limits>

const int BUFFER_SIZE = 1024;

// Функции-обработчики для разных типов сообщений
void handleMove(int clientSocket) {
    int move;
    std::string input;
    
    while (true) {
        std::getline(std::cin, input);
        try {
            move = std::stoi(input);
            if (move >= 1 && move <= 9) break;
        } catch (...) {}
        std::cout << "Неверный ввод. Введите число от 1 до 9: ";
    }

    std::string moveStr = std::to_string(move) + "\n";
    send(clientSocket, moveStr.c_str(), moveStr.size(), 0);
}

void handlePlayAgain(int clientSocket) {
    std::string response;
    while (true) {
        std::getline(std::cin, response);
        if (response == "да" || response == "нет") {
            send(clientSocket, response.c_str(), response.size(), 0);
            break;
        } else {
            std::cout << "Пожалуйста, введите 'да' или 'нет'.\n";
        }
    }
}

void handleAuthenticationChoice(int clientSocket) {
    std::string response;
    std::getline(std::cin, response);
    send(clientSocket, response.c_str(), response.size(), 0);
}

void handleLobbyChoice(int clientSocket) {
    std::string response;
    std::getline(std::cin, response);
    send(clientSocket, response.c_str(), response.size(), 0);
}

void handleAccountData(int clientSocket) {
    std::cout << "\nВведите имя пользователя: ";
    std::string username;
    std::getline(std::cin, username);
    std::cout << "\nВведите пароль пользователя: ";
    std::string password;
    std::getline(std::cin, password);

    std::string message = username + " " + password;
    send(clientSocket, message.c_str(), message.size(), 0);
}

void handleLobbyData(int clientSocket) {
    std::cout << "\nВведите название лобби: ";
    std::string lobbyname;
    std::getline(std::cin, lobbyname);
    std::cout << "Введите пароль лобби: ";
    std::string password;
    std::getline(std::cin, password);

    std::string message = lobbyname + " " + password;
    send(clientSocket, message.c_str(), message.size(), 0);
}

// Функция для выбора обработчика на основе сообщения
void processMessage(int clientSocket, const std::string& message, const std::map<std::string, std::function<void(int)>>& commandTable) {
    for (const auto& [key, handler] : commandTable) {
        if (message.find(key) != std::string::npos) {
            handler(clientSocket);
            return;
        }
    }
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Использование: " << argv[0] << " --connect <IP-адрес>:<порт>" << std::endl;
        return 1;
    }

    std::string address = argv[2];
    size_t colonPos = address.find(':');
    if (colonPos == std::string::npos) {
        std::cerr << "Неверный формат адреса. Используйте формат <IP-адрес>:<порт>" << std::endl;
        return 1;
    }
    std::string ip = address.substr(0, colonPos);
    int port = std::stoi(address.substr(colonPos + 1));

    int clientSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (clientSocket == -1) {
        std::cerr << "Ошибка создания сокета" << std::endl;
        return 1;
    }

    sockaddr_in serverAddress{};
    serverAddress.sin_family = AF_INET;
    inet_pton(AF_INET, ip.c_str(), &serverAddress.sin_addr);
    serverAddress.sin_port = htons(port);

    if (connect(clientSocket, (sockaddr*)&serverAddress, sizeof(serverAddress)) == -1) {
        std::cerr << "Ошибка подключения к серверу" << std::endl;
        close(clientSocket);
        return 1;
    }

    // Таблица команд
    std::map<std::string, std::function<void(int)>> commandTable = {
        {"Ваш ход. Введите номер клетки (1-9): ", handleMove},
        {"Хотите сыграть еще раз? (да/нет): ", handlePlayAgain},
        {"Выберите действие: 1 - Регистрация, 2 - Вход: ", handleAuthenticationChoice},
        {"Хотите создать лобби или присоединиться? (1 - Создать, 2 - Присоединиться, 3 - Выход): ", handleLobbyChoice},
        {"Введите данные аккаунта:", handleAccountData},
        {"Введите данные лобби:", handleLobbyData},
        {"Некорректный ввод, попробуйте снова.", handleMove},
    };

    char buffer[BUFFER_SIZE];

    while (true) {
        memset(buffer, 0, BUFFER_SIZE);
        int bytesReceived = recv(clientSocket, buffer, BUFFER_SIZE, 0);
        
        if (bytesReceived == 0) {
            std::cerr << "Соединение закрыто сервером." << std::endl;
            break;
        } else if (bytesReceived < 0) {
            std::cerr << "Ошибка при получении данных от сервера: " << strerror(errno) << std::endl;
            break;
        }

        std::string message(buffer);
        std::cout << message;

        // Обрабатываем сообщение с помощью таблицы команд
        processMessage(clientSocket, message, commandTable);
    }

    close(clientSocket);
    return 0;
}
