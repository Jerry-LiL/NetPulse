FROM debian:bookworm-slim AS build

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends build-essential pkg-config libsnmp-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY Makefile ./
COPY agent ./agent
RUN make

FROM debian:bookworm-slim

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends libsnmp40 \
    && rm -rf /var/lib/apt/lists/*

COPY --from=build /src/build/netpulse /usr/local/bin/netpulse
EXPOSE 8080
ENTRYPOINT ["netpulse"]
