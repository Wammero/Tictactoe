.PHONY: all build docker-db clean run-server init-db

DB_USER=myuser
DB_PASSWORD=mypassword
DB_NAME=mydb

all: docker-db init-db build run-server

docker-db:
	@docker ps -a --filter "name=my_postgres" | grep my_postgres > /dev/null || \
		docker run --name my_postgres -e POSTGRES_USER=$(DB_USER) -e POSTGRES_PASSWORD=$(DB_PASSWORD) -e POSTGRES_DB=$(DB_NAME) -p 5432:5432 -d postgres
	@docker start my_postgres > /dev/null || true

init-db:
	@docker exec -i my_postgres psql -U $(DB_USER) -d $(DB_NAME) < ./init/init.sql

build:
	@mkdir -p build
	@cd build && cmake .. && make

run-server:
	@./build/server/server --port 2020

clean:
	@docker stop my_postgres || true
	@docker rm my_postgres || true
	@rm -rf build
