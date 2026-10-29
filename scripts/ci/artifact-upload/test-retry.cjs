'use strict';

const assert = require('node:assert/strict');
const test = require('node:test');
const {retryControlRequests} = require('./retry.cjs');

test('finalization retries preserve the uploaded content and request', async () => {
  let uploads = 0, calls = 0;
  const payload = {name: 'artifact', hash: 'same-upload-digest'};
  const client = retryControlRequests({
    CreateArtifact: async () => { uploads++; return {ok: true}; },
    FinalizeArtifact: async request => {
      assert.strictEqual(request, payload);
      if (++calls < 3) throw new Error('Unable to make request: ECONNRESET');
      return {ok: true, artifactId: '123'};
    }
  }, {sleep: async () => {}});
  await client.CreateArtifact({});
  assert.equal((await client.FinalizeArtifact(payload)).artifactId, '123');
  assert.equal(uploads, 1);
  assert.equal(calls, 3);
});

test('authorization failures are surfaced immediately', async () => {
  let calls = 0;
  const client = retryControlRequests({
    CreateArtifact: async () => ({}),
    FinalizeArtifact: async () => { calls++; throw new Error('HTTP 403 Forbidden'); }
  }, {sleep: async () => {}});
  await assert.rejects(client.FinalizeArtifact({}), /403/);
  assert.equal(calls, 1);
});

test('persistent transport failures stop after the retry budget', async () => {
  let calls = 0;
  const client = retryControlRequests({
    CreateArtifact: async () => { calls++; throw Object.assign(new Error('connection interrupted'), {code: 'ETIMEDOUT'}); },
    FinalizeArtifact: async () => ({})
  }, {attempts: 3, sleep: async () => {}});
  await assert.rejects(client.CreateArtifact({}), /connection interrupted/);
  assert.equal(calls, 3);
});

test('official client uploads once when the final HTTP response disconnects', async () => {
  const http = require('node:http');
  const fs = require('node:fs');
  const os = require('node:os');
  const path = require('node:path');
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'gp-artifact-'));
  const previousEnv = {...process.env};
  let creates = 0, finalizes = 0, blocks = 0, commits = 0;
  const blockSizes = [];
  const server = http.createServer(async (request, response) => {
    let size = 0;
    for await (const chunk of request) size += chunk.length;
    if (request.method === 'PUT') {
      if (request.url.includes('comp=blocklist')) commits++;
      else { blocks++; blockSizes.push(size); }
      response.writeHead(201, {'etag': 'fixture', 'last-modified': new Date().toUTCString()});
      response.end();
    } else if (request.url.endsWith('/CreateArtifact')) {
      creates++;
      response.setHeader('Content-Type', 'application/json');
      response.end(JSON.stringify({ok: true, signed_upload_url: `http://127.0.0.1:${server.address().port}/account/container/blob?sig=fixture`}));
    } else if (request.url.endsWith('/FinalizeArtifact')) {
      if (++finalizes === 1) return request.socket.destroy();
      response.setHeader('Content-Type', 'application/json');
      response.end(JSON.stringify({ok: true, artifact_id: '123'}));
    } else {
      response.writeHead(404); response.end();
    }
  });
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  try {
    process.env.ACTIONS_RESULTS_URL = `http://127.0.0.1:${server.address().port}`;
    const payload = Buffer.from(JSON.stringify({scp: 'Actions.Results:fixture-run:fixture-job'})).toString('base64url');
    process.env.ACTIONS_RUNTIME_TOKEN = `fixture.${payload}.fixture`;
    process.env['INPUT_NAME'] = 'fixture-artifact';
    process.env['INPUT_ROOT-DIRECTORY'] = root;
    process.env['INPUT_PATH'] = path.join(root, 'deliverable.txt');
    process.env['INPUT_RETENTION-DAYS'] = '1';
    process.env.GITHUB_OUTPUT = path.join(root, 'output');
    process.env.GITHUB_STEP_SUMMARY = path.join(root, 'summary');
    fs.writeFileSync(process.env.INPUT_PATH, require('node:crypto').randomBytes(2 * 1024 * 1024 + 17));
    fs.writeFileSync(process.env.GITHUB_OUTPUT, '');
    fs.writeFileSync(process.env.GITHUB_STEP_SUMMARY, '');
    const {main} = await import('./upload.mjs');
    await main();
    assert.equal(creates, 1);
    assert.equal(blocks, 3);
    assert.ok(blockSizes.every(size => size <= 1024 * 1024), 'real HTTP blocks must not exceed 1 MiB');
    assert.equal(commits, 1);
    assert.equal(finalizes, 2);
    assert.match(fs.readFileSync(process.env.GITHUB_OUTPUT, 'utf8'), /123/);
  } finally {
    process.env = previousEnv;
    server.closeAllConnections();
    await new Promise(resolve => server.close(resolve));
    fs.rmSync(root, {recursive: true, force: true});
  }
});

test('blob retries preserve completed blocks and replay the same bytes', async () => {
  await import('./upload.mjs');
  const {BlockBlobClient} = await import('@azure/storage-blob');
  const http = require('node:http');
  const first = Buffer.from('first block');
  const second = Buffer.from('second block');
  const ids = ['block-A', 'block-B'].map(id => Buffer.from(id).toString('base64'));
  const received = new Map();
  const commits = [];
  const server = http.createServer(async (request, response) => {
    const body = [];
    for await (const part of request) body.push(part);
    const bytes = Buffer.concat(body);
    const url = new URL(request.url, 'http://localhost');
    if (url.searchParams.get('comp') === 'blocklist') {
      commits.push(bytes);
      if (commits.length === 1) return request.socket.destroy();
    } else {
      const id = url.searchParams.get('blockid');
      const values = received.get(id) || [];
      values.push(bytes); received.set(id, values);
      if (id === ids[1] && values.length === 1) return request.socket.destroy();
    }
    response.writeHead(201, {'etag': 'fixture', 'last-modified': new Date().toUTCString()});
    response.end();
  });
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  try {
    const client = new BlockBlobClient(`http://127.0.0.1:${server.address().port}/account/container/blob?sig=fixture`,
      undefined, {retryOptions: {maxTries: 1}});
    await client.stageBlock(ids[0], first, first.length);
    await client.stageBlock(ids[1], second, second.length);
    await client.commitBlockList(ids);
    assert.equal(received.get(ids[0]).length, 1);
    assert.equal(received.get(ids[1]).length, 2);
    assert.deepEqual(received.get(ids[1]), [second, second]);
    assert.equal(commits.length, 2);
    assert.deepEqual(commits[0], commits[1]);
  } finally {
    server.closeAllConnections();
    await new Promise(resolve => server.close(resolve));
  }
});

test('upload cancellation aborts in-flight block requests without waiting for a response', async () => {
  await import('./upload.mjs');
  const {BlockBlobClient} = await import('@azure/storage-blob');
  const {Readable} = require('node:stream');
  const http = require('node:http');
  let started;
  const received = new Promise(resolve => { started = resolve; });
  const server = http.createServer(async (request, response) => {
    request.on('error', () => {});
    started();
    // Simulate a remote connection that accepts bytes but never responds.
    request.resume();
  });
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  try {
    const client = new BlockBlobClient(`http://127.0.0.1:${server.address().port}/account/container/blob?sig=fixture`,
      undefined, {retryOptions: {maxTries: 1}});
    const controller = new AbortController();
    const uploading = client.uploadStream(Readable.from(Buffer.alloc(2 * 1024 * 1024)),
      8 * 1024 * 1024, 5, {abortSignal: controller.signal});
    const rejected = assert.rejects(uploading, error => error.name === 'AbortError');
    await received;
    controller.abort();
    await rejected;
  } finally {
    server.closeAllConnections();
    await new Promise(resolve => server.close(resolve));
  }
});
