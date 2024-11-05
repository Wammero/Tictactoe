# server/CMakeLists.txt
add_executable(server main.cpp)

# Подключение libpqxx и OpenSSL к серверу
target_link_libraries(server PRIVATE pqxx OpenSSL::SSL OpenSSL::Crypto)
