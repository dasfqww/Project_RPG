"""Loopback receipt fixture for UE process restart; not a backend/PG substitute."""
import http.server
import json
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
state = {"posts": 0, "commits": 0, "replays": 0, "errors": []}
original = None
actor = "00000001-0000-0001-0000-000100000001"
peer = "00000002-0000-0002-0000-000200000002"


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_POST(self):
        global original
        try:
            body = self.rfile.read(int(self.headers["Content-Length"]))
            data = json.loads(body)
            assert self.headers["Authorization"] == "persistence-fixture-player"
            assert self.path.startswith(f"/api/characters/{actor}/trades/") and self.path.endswith("/invite")
            assert set(data) == {"requestId", "inviteeCharacterId"} and data["inviteeCharacterId"] == peer
            state["posts"] += 1
            if original is None:
                original = (self.path, body)
                state["commits"] += 1
                self.reply(503, {"error": "fixture_lost_committed_response"}, "3600")
            else:
                assert original == (self.path, body), "different original bytes or route"
                state["replays"] += 1
                offer = {"currencyAmount": "0", "items": []}
                session = {"tradeId": self.path.split("/")[-2], "initiatorCharacterId": actor,
                           "inviteeCharacterId": peer, "currencyCode": "Gold", "revision": "1",
                           "offerRevision": "1", "expiresAt": "2030-01-01T00:00:00Z", "status": "Invited",
                           "completed": False, "initiatorOffer": offer, "inviteeOffer": offer,
                           "initiatorReady": False, "inviteeReady": False,
                           "initiatorConfirmed": False, "inviteeConfirmed": False}
                self.reply(200, {"requestId": data["requestId"], "committedAt": "2026-11-10T00:00:00Z",
                                 "rejection": None, "session": session})
        except Exception as exc:
            state["errors"].append(str(exc))
            self.reply(500, {"error": "fixture_failed"})
        finally:
            (root / "Stats.json").write_text(json.dumps(state), encoding="utf-8")

    def reply(self, code, value, retry=None):
        content = json.dumps(value).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(content)))
        if retry:
            self.send_header("Retry-After", retry)
        self.end_headers()
        self.wfile.write(content)


server = http.server.HTTPServer(("127.0.0.1", 0), Handler)
(root / "Port.txt").write_text(str(server.server_port), encoding="ascii")
server.serve_forever()
