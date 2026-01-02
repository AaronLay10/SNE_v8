FROM rust:1.84-bookworm AS build
WORKDIR /workspace
COPY Cargo.toml ./
COPY crates ./crates
COPY services/controller-sim ./services/controller-sim
COPY services/sentient-core ./services/sentient-core
COPY services/osc-bridge ./services/osc-bridge
RUN cargo build -p sentient-core --release

FROM debian:bookworm-slim
RUN useradd -r -u 10001 -g nogroup sentient
COPY --from=build /workspace/target/release/sentient-core /usr/local/bin/sentient-core
USER sentient
ENV RUST_LOG=info
ENTRYPOINT ["/usr/local/bin/sentient-core"]
