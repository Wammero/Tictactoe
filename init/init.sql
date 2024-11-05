DROP TABLE IF EXISTS sessions;
DROP TABLE IF EXISTS lobbies;

-- Таблица для хранения информации об игроках
CREATE TABLE IF NOT EXISTS players (
    id SERIAL PRIMARY KEY,
    username TEXT UNIQUE NOT NULL UNIQUE,
    password_hash TEXT NOT NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    last_login TIMESTAMP
);

-- Таблица для хранения статистики игроков
CREATE TABLE IF NOT EXISTS statistics (
    id SERIAL PRIMARY KEY,
    player_id INTEGER REFERENCES players(id) UNIQUE,
    games_played INTEGER DEFAULT 0,
    wins INTEGER DEFAULT 0,
    losses INTEGER DEFAULT 0,
    draws INTEGER DEFAULT 0
);

-- Таблица для хранения информации о матчах
CREATE TABLE IF NOT EXISTS matches (
    id SERIAL PRIMARY KEY,
    player1_id INTEGER REFERENCES players(id),
    player2_id INTEGER REFERENCES players(id),
    winner_id INTEGER REFERENCES players(id),
    match_time TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- Таблица для хранения информации о лобби
CREATE TABLE IF NOT EXISTS lobbies (
    id SERIAL PRIMARY KEY,
    name TEXT UNIQUE NOT NULL UNIQUE,
    password_hash TEXT,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    owner_id INTEGER REFERENCES players(id),
    is_full BOOLEAN DEFAULT FALSE  -- Поле для указания, заполнено ли лобби
);

-- Таблица для хранения информации о сессиях
CREATE TABLE IF NOT EXISTS sessions (
    id SERIAL PRIMARY KEY,
    player_id INTEGER REFERENCES players(id),
    socket INTEGER NOT NULL 
);
