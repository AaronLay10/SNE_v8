FROM rust:bookworm AS build
WORKDIR /workspace
COPY Cargo.toml ./
COPY Cargo.lock ./
COPY crates ./crates
COPY services/controller-sim ./services/controller-sim
COPY services/sentient-core ./services/sentient-core
COPY services/sentient-api ./services/sentient-api
COPY services/sentient-auth ./services/sentient-auth
COPY services/osc-bridge ./services/osc-bridge
COPY services/sentient-notify ./services/sentient-notify
COPY services/scs-sim ./services/scs-sim
RUN cargo build -p osc-bridge --release --locked

FROM debian:bookworm-slim
RUN useradd -r -u 10002 -g nogroup oscbridge
COPY --from=build /workspace/target/release/osc-bridge /usr/local/bin/osc-bridge
USER oscbridge
ENV RUST_LOG=info
ENTRYPOINT ["/usr/local/bin/osc-bridge"]
