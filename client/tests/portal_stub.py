#!/usr/bin/env python3
# portal_stub.py PORT
#
# A stand-in for the RenkuOSPorts portal's build-client API, for testing
# Awase without SQL Server and blob storage. It follows
# src/RenkuPorts.Server/Endpoints/BuildClientApi.cs in
# github.com/Druware/RenkuOSPorts-Portal (commit 4d4d2be): the same routes,
# JSON names, status codes and rules. Each claim of a job is a build; uploads
# are staged for the build (sha256 field before the file, 409 on a different
# package of the same name, packages of other ports recorded as extras);
# complete needs one of the port's packages and sends the job to
# verification, which publishes once REQUIRED_MATCHES builds have the same
# packages (here: the same sha256s; the portal compares contents), fails it
# when three builds all differ, and otherwise queues the next build. A client
# that completed a build may not claim the job again until its window
# (completion + the build's duration) has passed. fail fails the job and
# drops every build's uploads; release gives the job back. Lease expiry,
# GET /api/client/me and the paged package list are there too. Tokens are
# opaque here, not signed JWTs, but carry the same claims.
#
# Test-only routes, not in the portal:
#   POST /stub/clients {platform}            -> {clientId, secret}
#   POST /stub/jobs {port, version, platform} -> {jobId}
#   GET  /stub/state                          -> jobs, builds and packages
#   POST /stub/expire/<jobId>                 expire a job's lease now
#   POST /stub/settings {requiredMatches, portsRepo}
#
# Copyright 2026, Ian Dumych.
# SPDX-License-Identifier: MIT

import base64
import datetime
import hashlib
import json
import re
import secrets
import sys
import threading
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

LOCK = threading.Lock()
CLIENTS = {}     # id -> {secretHash, platform, name, enabled}
TOKENS = {}      # token -> client id
JOBS = []        # dicts, see stub_job
BUILDS = []      # dicts: id, jobId, number, clientId, status, claimedAt,
                 # completedAt, exclusiveUntil, staged [{fileName, sha256,
                 # size, buildDate, extra}]
PACKAGES = []    # published: platform, fileName, sha256, size, jobId, ...
HPKG_NAME = re.compile(r'^[A-Za-z0-9_+~][A-Za-z0-9._+~-]*\.hpkg$')
LEASE_HOURS = 72
SETTINGS = {'requiredMatches': 2,
            'portsRepo': {'url': 'https://example.invalid/RenkuOSPorts.git', 'branch': 'main'}}


def now():
    return datetime.datetime.now(datetime.timezone.utc)


def iso(t):
    return t.isoformat().replace('+00:00', 'Z') if t else None


def b64url(data):
    return base64.urlsafe_b64encode(data).rstrip(b'=').decode()


class Handler(BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'

    def log_message(self, fmt, *args):
        sys.stderr.write('stub: ' + fmt % args + '\n')

    # replies

    def send_json(self, code, body):
        data = json.dumps(body).encode()
        self.send_response(code)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def problem(self, code, title, detail=None):
        body = {'status': code, 'title': title}
        if detail:
            body['detail'] = detail
        self.send_json(code, body)

    def no_content(self):
        self.send_response(204)
        self.send_header('Content-Length', '0')
        self.end_headers()

    def body(self):
        length = int(self.headers.get('Content-Length') or 0)
        return self.rfile.read(length) if length else b''

    def json_body(self):
        try:
            return json.loads(self.body() or b'{}')
        except ValueError:
            return None

    def client(self):
        auth = self.headers.get('Authorization', '')
        token = auth[7:] if auth.startswith('Bearer ') else None
        cid = TOKENS.get(token)
        c = CLIENTS.get(cid)
        return (cid, c) if c and c['enabled'] else (None, None)

    def claimed_job(self, job_id, cid):
        job = next((j for j in JOBS if j['id'] == job_id), None)
        if job is None:
            self.problem(404, 'Not Found')
            return None
        if job['status'] != 'InProgress':
            self.problem(409, 'Job is ' + job['status'])
            return None
        if job['claimedBy'] != cid:
            self.problem(403, 'Job is claimed by another client')
            return None
        return job

    # routes

    def do_GET(self):
        with LOCK:
            path, _, query = self.path.partition('?')
            args = dict(a.partition('=')[::2] for a in query.split('&') if a)
            m = re.fullmatch(r'/api/repo/([^/]+)/packages', path)
            if m:
                published = [p for p in reversed(PACKAGES) if p['platform'] == m.group(1)]
                size = min(max(int(args.get('pageSize') or 50), 1), 200)
                page = max(int(args.get('page') or 1), 1)
                return self.send_json(200, {'items': [{
                    'fileName': p['fileName'], 'size': p['size'], 'sha256': p['sha256'],
                    'buildDate': p['buildDate'], 'uploadedAt': p['uploadedAt'],
                    'recipeUpdatedAt': p['uploadedAt'], 'portName': p['portName'],
                    'builtBy': 'stub builder', 'verification': p['verification'],
                    'verifiedClients': p['verifiedClients']}
                    for p in published[(page - 1) * size:page * size]],
                    'total': len(published), 'page': page, 'pageSize': size})
            if path == '/api/client/me':
                cid, c = self.client()
                if c is None:
                    return self.problem(401, 'Unauthorized')
                return self.send_json(200, {'id': cid, 'name': c['name'], 'platform': c['platform'],
                                            'owner': 'stub owner', 'currentJob': None, 'ownerClients': [],
                                            'portsRepo': SETTINGS['portsRepo']})
            if path == '/stub/state':
                return self.send_json(200, {'jobs': JOBS, 'builds': BUILDS, 'packages': PACKAGES})
        self.problem(404, 'Not Found')

    def do_POST(self):
        path = self.path.split('?')[0]
        with LOCK:
            if path == '/stub/clients':
                req = self.json_body() or {}
                cid, secret = str(uuid.uuid4()), secrets.token_hex(16)
                CLIENTS[cid] = {'secretHash': hashlib.sha256(secret.encode()).hexdigest(),
                                'platform': req.get('platform', 'x86_64'),
                                'name': req.get('name', 'test'), 'enabled': True}
                return self.send_json(200, {'clientId': cid, 'secret': secret})
            if path == '/stub/jobs':
                req = self.json_body() or {}
                job = stub_job(req['port'], req.get('version', '1.0'),
                               req.get('platform', 'x86_64'))
                return self.send_json(200, {'jobId': job['id']})
            if path == '/stub/settings':
                SETTINGS.update(self.json_body() or {})
                return self.no_content()
            m = re.fullmatch(r'/stub/expire/(\d+)', path)
            if m:
                for j in JOBS:
                    if j['id'] == int(m.group(1)):
                        j['leaseExpiresAt'] = iso(now() - datetime.timedelta(seconds=1))
                return self.no_content()

            if path == '/api/client/token':
                req = self.json_body() or {}
                c = CLIENTS.get(req.get('clientId') or '')
                if (not c or not c['enabled'] or not req.get('secret') or
                        hashlib.sha256(req['secret'].encode()).hexdigest() != c['secretHash']):
                    return self.problem(401, 'Invalid client id or secret')
                claims = json.dumps({'sub': req['clientId'], 'platform': c['platform']}).encode()
                token = b64url(b'{"alg":"none"}') + '.' + b64url(claims) + '.' + secrets.token_hex(8)
                TOKENS[token] = req['clientId']
                return self.send_json(200, {'accessToken': token,
                                            'expiresAt': iso(now() + datetime.timedelta(hours=12))})

            if not path.startswith('/api/client/jobs/'):
                return self.problem(404, 'Not Found')
            cid, c = self.client()
            if c is None:
                self.body()
                return self.problem(401, 'Unauthorized')

            if path == '/api/client/jobs/next':
                self.body()
                t = now()
                for j in JOBS:
                    if j['status'] == 'InProgress' and j['leaseExpiresAt'] < iso(t):
                        release(j)
                queued = [j for j in JOBS if j['status'] == 'Queued' and j['platform'] == c['platform']
                          and not any(b['jobId'] == j['id'] and b['status'] == 'Completed'
                                      and b['clientId'] == cid and b['exclusiveUntil'] > iso(t) for b in BUILDS)]
                if not queued:
                    return self.no_content()
                j = min(queued, key=lambda j: (j['queuedAt'], j['id']))
                j.update(status='InProgress', claimedBy=cid, claimedAt=iso(t),
                         leaseExpiresAt=iso(t + datetime.timedelta(hours=LEASE_HOURS)),
                         attempts=j['attempts'] + 1)
                number = sum(1 for b in BUILDS if b['jobId'] == j['id']) + 1
                BUILDS.append({'id': len(BUILDS) + 1, 'jobId': j['id'], 'number': number, 'clientId': cid,
                               'status': 'InProgress', 'claimedAt': iso(t), 'completedAt': None,
                               'exclusiveUntil': None, 'staged': []})
                return self.send_json(200, {'jobId': j['id'], 'portName': j['port'], 'version': j['version'],
                                            'category': 'dev-libs', 'recipePath': j['recipePath'],
                                            'platform': j['platform'], 'build': number})

            m = re.fullmatch(r'/api/client/jobs/(\d+)/(packages|complete|fail|release)', path)
            if not m:
                self.body()
                return self.problem(404, 'Not Found')
            raw = self.body()
            job = self.claimed_job(int(m.group(1)), cid)
            if job is None:
                return
            build = next((b for b in BUILDS if b['jobId'] == job['id'] and b['status'] == 'InProgress'
                          and b['clientId'] == cid), None)
            if build is None:
                return self.problem(409, 'The job has no open build of this client')
            action = m.group(2)
            try:
                req = json.loads(raw or b'{}') if action != 'packages' else {}
            except ValueError:
                req = {}

            if action == 'packages':
                return self.upload(job, build, raw)
            if action == 'complete':
                if not any(not p['extra'] for p in build['staged']):
                    return self.problem(400, 'Bad request',
                                        f"Upload at least one package of {job['port']} before completing the job.")
                if req.get('log') is not None:
                    job['log'] = req['log']
                t = now()
                claimed = datetime.datetime.fromisoformat(build['claimedAt'].replace('Z', '+00:00'))
                build.update(status='Completed', completedAt=iso(t), exclusiveUntil=iso(t + (t - claimed)))
                if (job['failureSummary'] or '').startswith('Released: '):
                    job['failureSummary'] = None
                job.update(status='Verifying', claimedBy=None, claimedAt=None, leaseExpiresAt=None)
                verify(job)
                return self.send_json(200, public_job(job))
            if action == 'release':
                release(job)
                reason = (req.get('reason') or '').strip() or 'no reason given'
                job['failureSummary'] = ('Released: ' + reason)[:2000]
                if req.get('log') is not None:
                    job['log'] = req['log']
                return self.no_content()
            # fail
            summary = (req.get('summary') or '').strip() or 'build failed'
            build.update(status='Failed', completedAt=iso(now()))
            job.update(status='Failed', failureSummary=summary[:2000], log=req.get('log'),
                       completedAt=iso(now()), claimedBy=None, claimedAt=None, leaseExpiresAt=None)
            for b in BUILDS:
                if b['jobId'] == job['id']:
                    b['staged'] = []
            return self.send_json(200, public_job(job))

    def upload(self, job, build, raw):
        ctype = self.headers.get('Content-Type', '')
        m = re.match(r'multipart/form-data;\s*boundary="?([^";]+)"?', ctype)
        if not m:
            return self.problem(400, 'Bad request', 'Expected multipart/form-data: sha256 and buildDate fields, then the \'file\' part.')
        boundary = b'--' + m.group(1).encode()
        fields, file_name, data = {}, None, None
        for part in raw.split(boundary)[1:]:
            if part.startswith(b'--'):
                break
            head, _, content = part.partition(b'\r\n\r\n')
            content = content[:-2] if content.endswith(b'\r\n') else content
            disposition = head.decode(errors='replace')
            name = re.search(r'name="([^"]*)"', disposition)
            fname = re.search(r'filename="([^"]*)"', disposition)
            if fname:
                if name is None or name.group(1) != 'file':
                    return self.problem(400, 'Bad request', "Missing 'file' part.")
                file_name, data = fname.group(1), content
                break   # like the server, fields after the file are not read
            fields[name.group(1) if name else ''] = content.decode()
        if file_name is None:
            return self.problem(400, 'Bad request', "Missing 'file' part.")
        if not HPKG_NAME.match(file_name) or '..' in file_name:
            return self.problem(400, 'Bad request', 'fileName must be a plain *.hpkg name without path separators.')
        sha = (fields.get('sha256') or '').lower()
        if not re.fullmatch(r'[0-9a-f]{64}', sha):
            return self.problem(400, 'Bad request', 'A sha256 field (64 hex digits) must come before the file part.')
        actual = hashlib.sha256(data).hexdigest()
        if actual != sha:
            return self.problem(400, 'Bad request', f'sha256 {sha} does not match the uploaded bytes ({actual}).')
        extra = not belongs_to_port(file_name, job['port'])
        staged = next((p for p in build['staged'] if p['fileName'] == file_name), None)
        if staged and staged['sha256'] != sha:
            return self.problem(409, 'Package already exists',
                                f"{file_name} was already uploaded for this build with sha256 {staged['sha256']}.")
        published = next((p for p in PACKAGES if p['platform'] == job['platform'] and p['fileName'] == file_name
                          and p['sha256'] != sha), None)
        if not extra and published:
            return self.problem(409, 'Package already exists',
                                f"{job['platform']}/{file_name} exists with different content (sha256 {published['sha256']}).")
        if not staged:
            build['staged'].append({'fileName': file_name, 'sha256': sha, 'size': len(data), 'extra': extra,
                                    'buildDate': fields.get('buildDate') or iso(now())})
        return self.send_json(200, {'fileName': file_name, 'sha256': sha, 'size': len(data), 'extra': extra})


def stub_job(port, version, platform):
    job = {'id': len(JOBS) + 1, 'port': port, 'version': version, 'platform': platform,
           'recipePath': f'dev-libs/{port}/{port}-{version}.recipe', 'status': 'Queued',
           'queuedAt': iso(now()), 'claimedBy': None, 'claimedAt': None, 'leaseExpiresAt': None,
           'completedAt': None, 'attempts': 0, 'failureSummary': None, 'log': None,
           'selfVerified': False}
    JOBS.append(job)
    return job


def belongs_to_port(file_name, port):
    # By name, as the portal does (without its check for longer port names).
    name = file_name.split('-')[0]
    return name == port or name.startswith(port + '_')


def digest(build):
    return tuple(sorted((p['fileName'], p['sha256']) for p in build['staged'] if not p['extra']))


def verify(job):
    """What the portal's BuildVerifier does once a build completes."""
    done = [b for b in BUILDS if b['jobId'] == job['id'] and b['status'] == 'Completed']
    for b in done:
        same = [o for o in done if digest(o) == digest(b)]
        if len(same) >= SETTINGS['requiredMatches']:
            clients = len({o['clientId'] for o in same})
            for p in b['staged']:
                if not p['extra']:
                    PACKAGES.append(dict(p, platform=job['platform'], jobId=job['id'], portName=job['port'],
                                         uploadedAt=iso(now()),
                                         verification='verified' if clients > 1 else
                                         'self-verified' if len(same) > 1 else None,
                                         verifiedClients=clients if len(same) > 1 else 0))
            job.update(status='Succeeded', completedAt=iso(now()), selfVerified=len(same) > 1 and clients == 1)
            return
    if len(done) >= 3:
        job.update(status='Failed', failureSummary='three builds differ', completedAt=iso(now()))
        return
    job['status'] = 'Queued'


def release(job):
    """Requeues an InProgress job; its open build becomes Expired."""
    job.update(status='Queued', claimedBy=None, claimedAt=None, leaseExpiresAt=None)
    for b in BUILDS:
        if b['jobId'] == job['id'] and b['status'] == 'InProgress':
            b.update(status='Expired', completedAt=iso(now()), staged=[])


def public_job(j):
    return {'id': j['id'], 'portName': j['port'], 'version': j['version'], 'platform': j['platform'],
            'status': j['status'], 'attempts': j['attempts'], 'failureSummary': j['failureSummary'],
            'packages': [p['fileName'] for p in PACKAGES if p['jobId'] == j['id']]}


if __name__ == '__main__':
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8099
    host = sys.argv[2] if len(sys.argv) > 2 else '127.0.0.1'
    ThreadingHTTPServer((host, port), Handler).serve_forever()
