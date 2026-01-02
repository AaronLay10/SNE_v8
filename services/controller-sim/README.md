# controller-sim

Development-only “virtual controller” that connects to the per-room MQTT broker and:

- Publishes periodic heartbeats
- Subscribes to per-device command topic
- Emits ACCEPTED + COMPLETED acks for received commands

This is used to validate Sentient’s end-to-end messaging loop before integrating real hardware.

