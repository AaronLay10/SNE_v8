# Runbook — Safety Reset (Latched) (v8)

Some faults (especially safety-related) are expected to be **latched** until a Technical operator explicitly resets them.

This runbook is the operational procedure; the implementation is still in-progress.

## Preconditions (must be true)

- Physical room is verified safe (walkthrough if required).
- E-stop (if used) is released and interlocks are satisfied.
- All involved devices report a safe state to Sentient (controller-side).
- The operator performing reset is authorized (Technical/Admin).

## Procedure (target flow)

1. Pause room execution (GM/Technical).
2. Identify the latched fault(s):
   - `room/<room>/core/fault`
   - `room/<room>/core/device/<device>/fault`
3. Verify device state and physical safety.
4. Perform a **dual-confirmed reset** action from the Technical UI (or a secured API endpoint).
   - Current v8 MVP control op: publish `CoreControlRequest` with `op = "RESET_SAFETY_LATCH"` (prefer via authenticated `sentient-api`).
5. Confirm faults clear and `room/<room>/core/status` returns to SAFE.
6. Re-arm critical dispatch only after confirmation.

## MVP API Example

```bash
curl -sS -X POST "http://<room_ip>:8080/v8/room/<room_id>/safety/reset/request" \
  -H "Authorization: Bearer <JWT or API_TOKEN (TECH/ADMIN)>" \
  -H "Content-Type: application/json" \
  -d '{"reason":"cleared after physical walkthrough"}'

# then confirm (must be within 60 seconds)
curl -sS -X POST "http://<room_ip>:8080/v8/room/<room_id>/safety/reset/confirm" \
  -H "Authorization: Bearer <JWT or API_TOKEN (TECH/ADMIN)>" \
  -H "Content-Type: application/json" \
  -d '{"reset_id":"<reset_id_from_request>"}'
```

## Notes

- Resets should be fully audited (who/when/what/why).
- Room should not “self-heal” safety latches on reconnect/restart.
