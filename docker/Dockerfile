# Multi-stage build
FROM ubuntu:22.04 as build
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake curl ca-certificates && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . .
RUN cmake -DCMAKE_BUILD_TYPE=Release -S . -B build && cmake --build build -j
 
FROM ubuntu:22.04
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates curl && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY --from=build /src/build/udp_server /app/bin/udp_server
COPY --from=build /src/build/udp_client /app/bin/udp_client
EXPOSE 9000/udp 9100/tcp
ENTRYPOINT ["/app/bin/udp_server","--port","9000","--metrics-port","9100","--batch","64","--verbose"]