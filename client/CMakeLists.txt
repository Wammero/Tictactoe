# client/CMakeLists.txt
add_executable(client main.cpp)

# Подключение libpqxx и OpenSSL к клиенту
target_link_libraries(client PRIVATE pqxx OpenSSL::SSL OpenSSL::Crypto)
