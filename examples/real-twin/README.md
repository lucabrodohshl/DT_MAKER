# Real twin example: pump P-102

Material for [Tutorial: connect a real twin](../../docs/studio/tutorial-connect-real-twin.md).

| File | What it is |
|---|---|
| `plc-p102.jsonl` | A recorded PLC log of one operating cycle of pump P-102: 40 messages, one JSON object per line, in the plant's own vocabulary (tags such as `TT101_degC`, event codes such as `START`, timestamps `ts` in epoch milliseconds). |
| `pump-p102.connection.json` | The plant connection of the instance: REST and MQTT endpoints on automatic ports, the timestamp field, and the plant names (tag → signal, code → event) of the centrifugal-pump Blueprint. It is the `connection` object of `POST /api/v1/instances` and the body of `PUT /api/v1/instances/{id}/connection`. |

Replay the log into a deployed instance (one request; the messages keep their original spacing in logical time):

```bash
curl -X POST http://<host>:<rest port>/v1/messages -H 'Authorization: Bearer <token>' \
  --data-binary @examples/real-twin/plc-p102.jsonl
```

The address, port and token are on the instance's **Plant connection** page.
