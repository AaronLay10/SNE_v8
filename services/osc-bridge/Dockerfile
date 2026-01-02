FROM rust:1.84-bookworm AS build
WORKDIR /workspace
COPY Cargo.toml ./
COPY crates ./crates
COPY services/controller-sim ./services/controller-sim
COPY services/sentient-core ./services/sentient-core
COPY services/osc-bridge ./services/osc-bridge
RUN cargo build -p osc-bridge --release

FROM debian:bookworm-slim
RUN useradd -r -u 10002 -g nogroup oscbridge
COPY --from=build /workspace/target/release/osc-bridge /usr/local/bin/osc-bridge
USER oscbridge
ENV RUST_LOG=info
ENTRYPOINT ["/usr/local/bin/osc-bridge"]
