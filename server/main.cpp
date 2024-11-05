#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <unordered_map>
#include <mutex>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <optional>
#include <pqxx/pqxx>
#include <openssl/sha.h> 

const int PORT = 2020;
const int BUFFER_SIZE = 1024;

std::mutex boardMutex;
std::unordered_map<int, bool> playAgainRequests;


struct GameSession {
    int player1Socket;
    int player2Socket;
    int player1Id;
    int player2Id;
    std::vector<char> board;
    std::string lobbyName;

    GameSession(int p1, int p2, int p1Id, int p2Id, const std::string& lobbyName) : player1Socket(p1), player2Socket(p2), player1Id(p1Id), player2Id(p2Id), lobbyName(lobbyName), board(9, ' ') {}
};

int sendMessage(int socket, const std::string& message) {
    return send(socket, message.c_str(), message.size(), 0);
}

std::string displayBoard(const std::vector<char>& board) {
    // Форматируем доску для отправки клиенту
    std::string boardState = "";
    for (int i = 0; i < 9; ++i) {
        boardState += board[i] == ' ' ? std::to_string(i + 1) : std::string(1, board[i]);
        if (i % 3 != 2) boardState += "|";
        if (i % 3 == 2 && i < 6) boardState += "\n-+-+-\n";
    }
    return boardState + "\n";
}

bool makeMove(std::vector<char>& board, int position, char playerSymbol) {
    // Проверка корректности хода
    if (position < 1 || position > 9 || board[position - 1] != ' ') return false;
    board[position - 1] = playerSymbol;
    return true;
}

bool checkWin(const std::vector<char>& board, char playerSymbol) {
    // Проверка выигрышных комбинаций
    const int winningCombos[8][3] = {
        {0, 1, 2}, {3, 4, 5}, {6, 7, 8}, // горизонтальные
        {0, 3, 6}, {1, 4, 7}, {2, 5, 8}, // вертикальные
        {0, 4, 8}, {2, 4, 6}             // диагональные
    };
    for (auto& combo : winningCombos) {
        if (board[combo[0]] == playerSymbol && board[combo[1]] == playerSymbol && board[combo[2]] == playerSymbol) {
            return true;
        }
    }
    return false;
}

void updateStatistics(pqxx::connection& C, int playerId, int result) {
    try {
        pqxx::work W(C);

        // Подготовка значений для обновления в зависимости от результата игры
        int winIncrement = (result == 1) ? 1 : 0;
        int lossIncrement = (result == -1) ? 1 : 0;
        int drawIncrement = (result == 0) ? 1 : 0;

        // Вставка или обновление записи в таблице statistics
        W.exec_params(
            "INSERT INTO statistics (player_id, games_played, wins, losses, draws) "
            "VALUES ($1, 1, $2, $3, $4) "
            "ON CONFLICT (player_id) DO UPDATE "
            "SET games_played = statistics.games_played + 1, "
            "    wins = statistics.wins + EXCLUDED.wins, "
            "    losses = statistics.losses + EXCLUDED.losses, "
            "    draws = statistics.draws + EXCLUDED.draws;",
            playerId, winIncrement, lossIncrement, drawIncrement
        );

        // Применяем изменения
        W.commit();

        std::cout << "Статистика обновлена для игрока с ID: " << playerId << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Ошибка при обновлении статистики: " << e.what() << std::endl;
    }
}

bool deleteSessionByPlayerId(pqxx::connection& C, int playerId) {
    try {
        pqxx::work W(C);
        
        // Удаляем запись с указанным player_id из таблицы sessions
        W.exec_params("DELETE FROM sessions WHERE player_id = $1;", playerId);
        
        W.commit();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Ошибка при удалении сессии: " << e.what() << std::endl;
        return false;
    }
}

bool deleteLobbyById(pqxx::connection& C, std::string& lobbyName) {
    try {
        pqxx::work W(C);
        
        // Удаляем запись с указанным player_id из таблицы sessions
        W.exec_params("DELETE FROM lobbies WHERE name = $1;", lobbyName);
        
        W.commit();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Ошибка при удалении лобби: " << e.what() << std::endl;
        return false;
    }
}

void insertMatch(pqxx::connection& C, int player1Id, int player2Id, std::optional<int> winner) {
    try {
        pqxx::work W(C);

        // Вставка новой записи в таблицу matches, если есть победитель или ничья
        if (winner.has_value()) {
            W.exec_params(
                "INSERT INTO matches (player1_id, player2_id, winner_id) "
                "VALUES ($1, $2, $3);",
                player1Id, player2Id, winner.value()
            );
            W.commit();

            updateStatistics(C, winner.value(), 1);
            updateStatistics(C, winner.value() == player1Id ? player2Id : player1Id, -1);
        } else {
            W.exec_params(
                "INSERT INTO matches (player1_id, player2_id, winner_id) "
                "VALUES ($1, $2, NULL);",
                player1Id, player2Id
            );
            W.commit();

            updateStatistics(C, winner.value(), 0);
            updateStatistics(C, winner.value() == player1Id ? player2Id : player1Id, 0);
        }

        

    } catch (const std::exception& e) {
        std::cerr << "Ошибка при записи матча: " << e.what() << std::endl;
    }
}

void handleGameSession(GameSession session, pqxx::connection& C) {
    bool playAgain = true;

    while (playAgain) {
        // Сброс доски
        session.board.assign(9, ' '); // Инициализируем пустую доску

        // Уведомляем игроков, что игра началась
        sendMessage(session.player1Socket, "Игра началась! Вы играете за 'X'.\n");
        sendMessage(session.player2Socket, "Игра началась! Вы играете за 'O'.\n");

        bool gameOn = true;
        int currentPlayerSocket = session.player1Socket;
        int currentPlayerId = session.player1Id;
        char currentPlayerSymbol = 'X';

        while (gameOn) {
            // Отправляем текущую доску обоим игрокам
            std::string boardState = "Текущая доска:\n" + displayBoard(session.board);
            sendMessage(session.player1Socket, boardState);
            sendMessage(session.player2Socket, boardState);

            // Отправляем игроку запрос на ход
            sendMessage(currentPlayerSocket, "Ваш ход. Введите номер клетки (1-9): ");

            // Получаем ход от текущего игрока
            char buffer[BUFFER_SIZE];
            int bytesReceived = recv(currentPlayerSocket, buffer, BUFFER_SIZE, 0);
            if (bytesReceived <= 0) {
                std::cerr << "Ошибка при получении данных от игрока.\n";
                gameOn = false;
                break;
            }
            buffer[bytesReceived] = '\0';

            int position = 0;
            try {
                position = std::stoi(buffer);
            } catch (const std::invalid_argument&) {
                sendMessage(currentPlayerSocket, "Некорректный ввод, попробуйте снова.\n");
                continue;
            }

            std::cout<<currentPlayerId << "-" << position << '\n';

            // Проверяем и делаем ход
            std::lock_guard<std::mutex> lock(boardMutex);
            if (makeMove(session.board, position, currentPlayerSymbol)) {
                if (checkWin(session.board, currentPlayerSymbol)) {
                    std::string winMessage = "Игрок " + std::string(1, currentPlayerSymbol) + " выиграл!\n";
                    sendMessage(session.player1Socket, winMessage);
                    sendMessage(session.player2Socket, winMessage);
                    // Отправляем финальную доску
                    std::string finalBoard = "Финальная доска:\n" + displayBoard(session.board);
                    sendMessage(session.player1Socket, finalBoard);
                    sendMessage(session.player2Socket, finalBoard);

                    std::cout << "WIN: " << currentPlayerId << '\n';
                    insertMatch(C, session.player1Id, session.player2Id, currentPlayerId);
                    gameOn = false;
                } else if (std::none_of(session.board.begin(), session.board.end(), [](char cell) { return cell == ' '; })) {
                    // Если доска полная, объявляем ничью
                    sendMessage(session.player1Socket, "Ничья!\n");
                    sendMessage(session.player2Socket, "Ничья!\n");
                    // Отправляем финальную доску
                    std::string finalBoard = "Финальная доска:\n" + displayBoard(session.board);
                    sendMessage(session.player1Socket, finalBoard);
                    sendMessage(session.player2Socket, finalBoard);

                    std::cout << "DRAW" << '\n'; 
                    insertMatch(C, session.player1Id, session.player2Id, std::nullopt);
                    gameOn = false;
                } else {
                    // Переход хода к следующему игроку
                    currentPlayerId = (currentPlayerId == session.player1Id) ? session.player2Id : session.player1Id;
                    currentPlayerSocket = (currentPlayerSocket == session.player1Socket) ? session.player2Socket : session.player1Socket;
                    currentPlayerSymbol = (currentPlayerSymbol == 'X') ? 'O' : 'X';
                }
            } else {
                sendMessage(currentPlayerSocket, "Некорректный ход, попробуйте снова.\n");
            }
        }

        // Запросить у игроков, хотят ли они сыграть еще раз
        std::string replayMessage = "Хотите сыграть еще раз? (да/нет): ";
        sendMessage(session.player1Socket, replayMessage);
        sendMessage(session.player2Socket, replayMessage);

        char replayBuffer[BUFFER_SIZE];
        bool player1WantsToPlayAgain = false;
        bool player2WantsToPlayAgain = false;
        
        memset(replayBuffer, 0, BUFFER_SIZE);

        recv(session.player1Socket, replayBuffer, BUFFER_SIZE, 0);
        std::string player1Response(replayBuffer);
        player1WantsToPlayAgain = (player1Response == "да");

        memset(replayBuffer, 0, BUFFER_SIZE);

        recv(session.player2Socket, replayBuffer, BUFFER_SIZE, 0);
        std::string player2Response(replayBuffer);
        player2WantsToPlayAgain = (player2Response == "да");

        playAgain = player1WantsToPlayAgain && player2WantsToPlayAgain;

        // Если игроки хотят сыграть еще раз, меняем символы
        if (playAgain) {
            // Поменять роли игроков
            std::swap(session.player1Socket, session.player2Socket);
            // Переопределяем текущие символы
            currentPlayerSymbol = (currentPlayerSymbol == 'X') ? 'O' : 'X'; // Следующий игрок
        }
    }

    deleteSessionByPlayerId(C, session.player1Id);
    deleteSessionByPlayerId(C, session.player2Id);
    deleteLobbyById(C, session.lobbyName);
    close(session.player1Socket);
    close(session.player2Socket);

}

// Функция для хеширования пароля
std::string hashPassword(const std::string& password) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(password.c_str()), password.size(), hash);

    std::ostringstream oss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }
    return oss.str();
}

bool addSession(pqxx::connection& C, int playerId, int socket) {
    try {
        pqxx::work W(C);
        pqxx::result R = W.exec_params("INSERT INTO sessions (player_id, socket) VALUES ($1, $2);", playerId, socket);
        
        // Проверяем, был ли успешно выполнен запрос
        if (R.affected_rows() == 0) {
            std::cerr << "Не удалось добавить сессию для player_id: " << playerId << std::endl;
            return false; // Возвращаем false, если сессия не добавлена
        }

        W.commit(); // Фиксируем транзакцию только после успешного выполнения
        return true;  // Возвращаем true, если сессия успешно добавлена
    } catch (const std::exception& e) {
        std::cerr << "Ошибка добавления сессии: " << e.what() << std::endl;
        return false;
    }
}

// Регистрация пользователя
std::optional<int> registerUser(pqxx::connection& C, const std::string& username, const std::string& password, int socket) {
    try {
        pqxx::work W(C);
        std::string passwordHash = hashPassword(password);
        pqxx::result R = W.exec_params(
            "INSERT INTO players (username, password_hash) VALUES ($1, $2) ON CONFLICT DO NOTHING RETURNING id;",
            username, passwordHash
        );

        if (!R.empty()) {
            int newPlayerId = R[0][0].as<int>();  // Получаем id нового игрока
            W.commit();
            addSession(C, newPlayerId, socket);    // Добавляем сессию
            return newPlayerId;  // Возвращаем id нового пользователя
        } else {
            W.commit();
            std::cerr << "Ошибка: пользователь с таким именем уже существует.\n";
            return std::nullopt;
        }
    } catch (const std::exception& e) {
        std::cerr << "Ошибка регистрации: " << e.what() << std::endl;
        return std::nullopt;
    }
}

// Авторизация пользователя
std::optional<int> authenticateUser(pqxx::connection& C, const std::string& username, const std::string& password, int socket) {
    try {
        pqxx::work W(C);
        std::string passwordHash = hashPassword(password);
        pqxx::result R = W.exec_params("SELECT id FROM players WHERE username = $1 AND password_hash = $2;", username, passwordHash);
        
        if (!R.empty()) {
            int playerId = R[0][0].as<int>();  // Получаем id игрока
            W.commit();
            addSession(C, playerId, socket);    // Добавляем сессию
            return playerId;  // Возвращаем id пользователя
        }
        W.commit();
        return std::nullopt;  // Если игрок не найден
    } catch (const std::exception& e) {
        std::cerr << "Ошибка авторизации: " << e.what() << std::endl;
        return std::nullopt;
    }
}

// Создание лобби
bool createLobby(pqxx::connection& C, const std::string& lobbyName, const std::string& lobbyPassword, int owner_id) {
    try {
        pqxx::work W(C);
        std::string passwordHash = hashPassword(lobbyPassword);
        pqxx::result R = W.exec_params(
            "INSERT INTO lobbies (name, password_hash, owner_id) VALUES ($1, $2, $3) ON CONFLICT DO NOTHING RETURNING id;",
            lobbyName, passwordHash, owner_id
        );

        W.commit();
        if (!R.empty()) {
            return true;  // Лобби создано успешно
        } else {
            std::cerr << "Ошибка: лобби с таким именем уже существует.\n";
            return false;
        }
    } catch (const std::exception& e) {
        std::cerr << "Ошибка создания лобби: " << e.what() << std::endl;
        return false;
    }
}

// Функция для присоединения к лобби и уведомления владельца
bool joinLobby(pqxx::connection& C, const std::string& lobbyName, const std::string& lobbyPassword, int secondPlayerId, int secondPlayerSocket) {
    try {
        pqxx::work W(C);
        std::string passwordHash = hashPassword(lobbyPassword);

        // Проверяем наличие лобби с указанным именем, паролем и что лобби не заполнено
        pqxx::result R = W.exec_params("SELECT owner_id, is_full FROM lobbies WHERE name = $1 AND password_hash = $2;", lobbyName, passwordHash);
        
        if (R.empty()) {
            std::cerr << "Лобби не найдено или пароль неверен." << std::endl;
            W.commit();
            return false;
        }

        int ownerId = R[0]["owner_id"].as<int>();
        bool isFull = R[0]["is_full"].as<bool>();

        if (isFull) {
            std::cerr << "Лобби уже заполнено." << std::endl;
            W.commit();
            return false;
        }

        // Получаем socket владельца из таблицы sessions
        pqxx::result socketRes = W.exec_params("SELECT socket FROM sessions WHERE player_id = $1;", ownerId);
        
        if (socketRes.empty()) {
            std::cerr << "Сокет для владельца лобби не найден." << std::endl;
            W.commit();
            return false;
        }

        int ownerSocket = socketRes[0]["socket"].as<int>();

        // Обновляем лобби на заполненное
        W.exec_params("UPDATE lobbies SET is_full = TRUE WHERE id = (SELECT id FROM lobbies WHERE name = $1 AND password_hash = $2);", lobbyName, passwordHash);
        
        W.commit();
    
        sendMessage(secondPlayerId, "Добро пожаловать в игру Крестики-Нолики!\n");

        GameSession session(ownerSocket, secondPlayerSocket, ownerId, secondPlayerId, lobbyName);
        handleGameSession(session, C);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Ошибка при присоединении к лобби: " << e.what() << std::endl;
        return false;
    }
}


// Функция для обработки запросов клиента
void handleClient(int clientSocket, pqxx::connection& C) {
    char buffer[BUFFER_SIZE];
    bool authenticated = false;
    bool inLobby = false;
    int playerId;

    // Регистрация или вход
    while(true) {
        std::string authMessage = "Выберите действие: 1 - Регистрация, 2 - Вход: ";
        sendMessage(clientSocket, authMessage);

        memset(buffer, 0, BUFFER_SIZE);
        recv(clientSocket, buffer, BUFFER_SIZE, 0);
        std::string authResponse(buffer);
        memset(buffer, 0, BUFFER_SIZE);

        if (authResponse == "1") {
            // Регистрация
            while(true) {
                std::string regMessage = "Введите данные аккаунта: ";
                sendMessage(clientSocket, regMessage);

                memset(buffer, 0, BUFFER_SIZE);

                recv(clientSocket, buffer, BUFFER_SIZE, 0);
                std::string userData(buffer);
                memset(buffer, 0, BUFFER_SIZE);

                int count = std::count(userData.begin(), userData.end(), ' ');

                if(count != 1) {
                    std::string errorMsg = "Не используйте пробелы\n";
                    sendMessage(clientSocket, errorMsg);
                    continue;
                }

                auto it = userData.find(" ");
                std::string username = userData.substr(0, it);
                std::string password = userData.substr(it + 1);

                if(username.empty() || password.empty()) {
                    std::string errorMsg = "Заполните поля\n";
                    sendMessage(clientSocket, errorMsg);
                    continue;
                }

                if (auto newPlayerId = registerUser(C, username, password, clientSocket)) {
                    std::cout << "Регистрация завершена для пользователя: " << username << std::endl;
                    authenticated = true;
                    playerId = *newPlayerId;  // Сохраняем id нового игрока
                    break;
                } else {
                    std::string errorMsg = "Ошибка регистрации. Попробуйте другое имя.\n";
                    sendMessage(clientSocket, errorMsg);
                    continue;
                }
            }
        } else if (authResponse == "2") {
            while(true) {
                // Авторизация
                std::string loginMessage = "Введите данные аккаунта: ";
                sendMessage(clientSocket, loginMessage);

                recv(clientSocket, buffer, BUFFER_SIZE, 0);
                std::string loginData(buffer);
                memset(buffer, 0, BUFFER_SIZE);

                int count = std::count(loginData.begin(), loginData.end(), ' ');

                if(count != 1) {
                    std::string errorMsg = "Не используйте пробелы\n";
                    sendMessage(clientSocket, errorMsg);
                    continue;
                }

                std::string username = loginData.substr(0, loginData.find(" "));
                std::string password = loginData.substr(loginData.find(" ") + 1);


                if(username.empty() || password.empty()) {
                    std::string errorMsg = "Заполните поля\n";
                    sendMessage(clientSocket, errorMsg);
                    continue;
                }


                if (auto existingPlayerId = authenticateUser(C, username, password, clientSocket)) {
                    std::cout << "Пользователь вошел: " << username << std::endl;
                    authenticated = true;
                    playerId = *existingPlayerId;  // Сохраняем id игрока
                } else {
                    std::string errorMsg = "Ошибка входа. Неверные данные.\n";
                    sendMessage(clientSocket, errorMsg);
                    continue;
                }
                break;
            }
        } else {
            std::string errorMsg = "Введите число от 1 до 2.\n";
            sendMessage(clientSocket, errorMsg);
            continue;
        }
        break;
    }


    if (authenticated) {
        while(true) { 
            // Лобби
            std::string lobbyMessage = "Хотите создать лобби или присоединиться? (1 - Создать, 2 - Присоединиться, 3 - Выход): ";
            sendMessage(clientSocket, lobbyMessage);

            recv(clientSocket, buffer, BUFFER_SIZE, 0);
            std::string lobbyResponse(buffer);
            memset(buffer, 0, BUFFER_SIZE);

            if (lobbyResponse == "1") {
                // Создание лобби
                std::string createLobbyMessage = "Введите данные лобби: ";
                sendMessage(clientSocket, createLobbyMessage);

                recv(clientSocket, buffer, BUFFER_SIZE, 0);
                std::string lobbyData(buffer);
                memset(buffer, 0, BUFFER_SIZE);

                int count = std::count(lobbyData.begin(), lobbyData.end(), ' ');

                if(count != 1) {
                    std::string errorMsg = "Не используйте пробелы\n";
                    sendMessage(clientSocket, errorMsg);
                    continue;
                }

                std::string lobbyName = lobbyData.substr(0, lobbyData.find(" "));
                std::string lobbyPassword = lobbyData.substr(lobbyData.find(" ") + 1);

                if(lobbyName.empty() || lobbyPassword.empty()) {
                    std::string errorMsg = "Заполните поля\n";
                    sendMessage(clientSocket, errorMsg);
                    continue;
                }

                if (createLobby(C, lobbyName, lobbyPassword, playerId)) {
                    std::cout << "Лобби создано с именем: " << lobbyName << std::endl;
                    inLobby = true;
                } else {
                    std::string errorMsg = "Ошибка создания лобби.\n";
                    sendMessage(clientSocket, errorMsg);
                    continue;
                }
                sendMessage(clientSocket, "Добро пожаловать в игру Крестики-Нолики! Ожидайте второго игрока...\n");
                while(true) {};

            } else if (lobbyResponse == "2") {
                // Присоединение к лобби
                std::string joinLobbyMessage = "Введите данные лобби: ";
                sendMessage(clientSocket, joinLobbyMessage);

                recv(clientSocket, buffer, BUFFER_SIZE, 0);
                std::string lobbyData(buffer);
                memset(buffer, 0, BUFFER_SIZE);

                int count = std::count(lobbyData.begin(), lobbyData.end(), ' ');

                if(count != 1) {
                    std::string errorMsg = "Не используйте пробелы\n";
                    sendMessage(clientSocket, errorMsg);
                    continue;
                }

                std::string lobbyName = lobbyData.substr(0, lobbyData.find(" "));
                std::string lobbyPassword = lobbyData.substr(lobbyData.find(" ") + 1);

                if(lobbyName.empty() || lobbyPassword.empty()) {
                    std::string errorMsg = "Заполните поля\n";
                    sendMessage(clientSocket, errorMsg);
                    continue;
                }

                if (joinLobby(C, lobbyName, lobbyPassword, playerId, clientSocket)) {
                    std::cout << "Присоединение к лобби с именем: " << lobbyName << std::endl;
                    inLobby = true;
                } else {
                    std::string errorMsg = "Ошибка при присоединении к лобби. Неверные данные.\n";
                    sendMessage(clientSocket, errorMsg);
                    continue;
                }

            } else if (lobbyResponse == "3") {
                deleteSessionByPlayerId(C, playerId);
                close(clientSocket);
                break;
            } else {
                std::string errorMsg = "Введите число от 1 до 3.\n";
                sendMessage(clientSocket, errorMsg);
                continue;
            }
        }
    }
}

// Основная функция
int main() {
    // Инициализация PostgreSQL из docker
    pqxx::connection C("postgresql://myuser:mypassword@0.0.0.0:5432/mydb?sslmode=disable");

    int serverSocket, clientSocket;
    struct sockaddr_in serverAddr, clientAddr;

    serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(PORT);

    bind(serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr));
    listen(serverSocket, 3);

    std::cout << "Сервер запущен на порту " << PORT << std::endl;

    while (true) {
        socklen_t addrLen = sizeof(clientAddr);
        clientSocket = accept(serverSocket, (struct sockaddr*)&clientAddr, &addrLen);
        std::thread(handleClient, clientSocket, std::ref(C)).detach();
    }

    close(serverSocket);
    return 0;
}
