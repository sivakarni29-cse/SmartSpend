FROM gcc:latest

WORKDIR /app

COPY C_project/server.c .

RUN gcc server.c -o server

EXPOSE 8080

WORKDIR /var/data

CMD ["/app/server"]