FROM gcc:latest

RUN apt-get update && \
    apt-get install -y --no-install-recommends libssl-dev && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY C_project/server_new.c .
COPY C_project/auth.h .

RUN gcc server_new.c -o server -lssl -lcrypto

EXPOSE 8080

WORKDIR /var/data

CMD ["/app/server"]