'use strict';

const transient = /\b(ECONNRESET|ETIMEDOUT|EPIPE|EAI_AGAIN|ENETUNREACH|ECONNREFUSED)\b|socket hang up|TLS connection/i;

function retryRequests(client, methods, {attempts = 5, sleep = ms => new Promise(resolve => setTimeout(resolve, ms)), onRetry = () => {}} = {}) {
  for (const method of methods) {
    const request = client[method];
    client[method] = async function (...args) {
      for (let attempt = 1; ; attempt++) {
        try {
          return await request.apply(this, args);
        } catch (error) {
          if (attempt >= attempts || !transient.test(`${error.code || ''} ${error.message || ''}`)) throw error;
          onRetry(method, attempt, attempts);
          await sleep(Math.min(1000 * 2 ** (attempt - 1), 8000));
        }
      }
    };
  }
  return client;
}

const retryControlRequests = (client, options) => retryRequests(client, ['CreateArtifact', 'FinalizeArtifact'], options);
const retryBlobRequests = (client, options) => retryRequests(client, ['stageBlock', 'commitBlockList'], {attempts: 12, ...options});

function boundBlobUploads(client, {blockSize = 1024 * 1024, concurrency = 6} = {}) {
  const signal = Symbol('uploadAbortSignal');
  const upload = client.uploadStream;
  const stage = client.stageBlock;
  client.uploadStream = async function (stream, bufferSize = blockSize, maxConcurrency = concurrency, options = {}) {
    this[signal] = options.abortSignal;
    try {
      return await upload.call(this, stream, Math.min(bufferSize, blockSize), Math.min(maxConcurrency, concurrency), options);
    } finally {
      delete this[signal];
    }
  };
  // storage-blob 12.34.0's uploadStream omits abortSignal on stageBlock.
  // Forward it so the official client's stalled-progress timer cancels I/O.
  client.stageBlock = function (id, body, length, options = {}) {
    return stage.call(this, id, body, length, {...options, abortSignal: this[signal] || options.abortSignal});
  };
  return client;
}

module.exports = {retryControlRequests, retryBlobRequests, boundBlobUploads};
