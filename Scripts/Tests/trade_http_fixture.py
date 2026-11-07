"""Owned loopback wire fixture: commit once, lose first response, replay exact bytes.

Not the production backend. Native UE tests exercise the real HTTP implementation here;
the existing API/PostgreSQL suites own server transaction coverage.
"""
import argparse
import json
import socket
import threading
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--port-file', required=True)
parser.add_argument('--stats-file', required=True)
args = parser.parse_args()
lock = threading.Lock()
receipts = {}
sessions = {}
stats = {'commits': 0, 'posts': 0, 'replays': 0, 'gets': 0, 'unauthorized': 0, 'errors': []}


def save():
    Path(args.stats_file).write_text(json.dumps(stats), encoding='utf-8')


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def reply(self, code, payload):
        body = json.dumps(payload, separators=(',', ':')).encode()
        self.send_response(code)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Cache-Control', 'no-store')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def handle_request(self, write):
        with lock:
            if self.headers.get('Authorization') != 'Bearer trade-test-player':
                stats['unauthorized'] += 1
                save()
                return self.reply(401, {'error': 'unauthorized'})
            parts = self.path.split('/')
            try:
                assert parts[:3] == ['', 'api', 'characters'] and parts[4] == 'trades'
                character = str(uuid.UUID(parts[3]))
                assert self.headers.get('Accept') == 'application/json'
                assert 'no-store' in self.headers.get('Cache-Control', '')
                if not write:
                    stats['gets'] += 1
                    save()
                    return self.reply(200, {'session': sessions.get(parts[5]) if len(parts) == 6 else None})
                stats['posts'] += 1
                assert len(parts) == 7 and parts[6] == 'invite'
                trade = str(uuid.UUID(parts[5]))
                assert self.headers.get('Content-Type') == 'application/json'
                length = int(self.headers['Content-Length'])
                assert 0 < length <= 16384
                body = self.rfile.read(length)
                command = json.loads(body)
                assert set(command) == {'requestId', 'inviteeCharacterId'}
                request_id = str(uuid.UUID(command['requestId']))
                invitee = str(uuid.UUID(command['inviteeCharacterId']))
                assert character != invitee
                if request_id in receipts:
                    original, receipt = receipts[request_id]
                    assert body == original and receipt['session']['tradeId'] == trade
                    stats['replays'] += 1
                    save()
                    return self.reply(200, receipt)
                session = dict(tradeId=trade, initiatorCharacterId=character, inviteeCharacterId=invitee,
                               currencyCode='Gold', revision='1', offerRevision='1', expiresAt='2030-01-01T00:00:00Z',
                               status='Invited', completed=False,
                               initiatorOffer={'currencyAmount': '0', 'items': []},
                               inviteeOffer={'currencyAmount': '0', 'items': []},
                               initiatorReady=False, inviteeReady=False, initiatorConfirmed=False, inviteeConfirmed=False)
                receipt = dict(requestId=request_id, session=session, rejection=None, committedAt='2026-11-08T00:00:00Z')
                receipts[request_id] = body, receipt
                sessions[trade] = session
                stats['commits'] += 1
                save()
                # Simulate a commit whose response never reaches the client.
                self.close_connection = True
                self.connection.shutdown(socket.SHUT_RDWR)
                self.connection.close()
            except Exception as error:
                stats['errors'].append(type(error).__name__ + ': ' + str(error))
                save()
                self.reply(400, {'error': 'fixture_contract_failed'})

    def do_GET(self):
        self.handle_request(False)

    def do_POST(self):
        self.handle_request(True)


server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
save()
Path(args.port_file).write_text(str(server.server_port), encoding='ascii')
server.serve_forever()
