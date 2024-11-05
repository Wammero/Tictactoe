cmake_minimum_required(VERSION 3.10)
project(TicTacToe)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Подключаем FetchContent для загрузки libpqxx
include(FetchContent)

FetchContent_Declare(
  libpqxx
  GIT_REPOSITORY https://github.com/jtv/libpqxx.git
  GIT_TAG 7.6.0  # Попробуем стабильную версию
)

FetchContent_MakeAvailable(libpqxx)

# Находим библиотеку OpenSSL
find_package(OpenSSL REQUIRED)

# Добавление подпроектов client и server
add_subdirectory(client)
add_subdirectory(server)
