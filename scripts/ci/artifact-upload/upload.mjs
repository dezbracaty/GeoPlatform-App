import fs from 'node:fs';
import path from 'node:path';
import {pathToFileURL} from 'node:url';
import * as core from '@actions/core';
import * as glob from '@actions/glob';
import {DefaultArtifactClient} from '@actions/artifact';
import {BlockBlobClient} from '@azure/storage-blob';
import {retryControlRequests, retryBlobRequests, boundBlobUploads} from './retry.cjs';

// @actions/artifact 6.3.1 aborts immediately on ECONNRESET. Wrap the pinned
// generated client so FinalizeArtifact retries preserve the uploaded blob.
const {ArtifactServiceClientJSON} = await import(new URL('./generated/index.js', import.meta.resolve('@actions/artifact')));
const retryOptions = {
  onRetry: (method, attempt, attempts) => core.warning(`Artifact ${method} connection interrupted; retry ${attempt}/${attempts - 1}`)
};
retryControlRequests(ArtifactServiceClientJSON.prototype, retryOptions);
// Small requests reduce retransmission cost on the runner's unstable link.
boundBlobUploads(BlockBlobClient.prototype);
// uploadStream holds each block's Buffer until stageBlock resolves. Repeating
// the same block ID and bytes safely preserves already completed blocks.
retryBlobRequests(BlockBlobClient.prototype, retryOptions);

export async function main() {
  const name = core.getInput('name', {required: true});
  const root = path.resolve(core.getInput('root-directory', {required: true}));
  const search = await glob.create(core.getInput('path', {required: true}));
  const files = (await search.glob()).filter(file => fs.statSync(file).isFile());
  if (!files.length) throw new Error('No artifact files were found');
  for (const file of files) {
    const relative = path.relative(root, file);
    if (relative.startsWith(`..${path.sep}`) || path.isAbsolute(relative)) throw new Error('Artifact file is outside the run directory');
  }
  const result = await new DefaultArtifactClient().uploadArtifact(name, files, root, {
    compressionLevel: 0,
    retentionDays: Number(core.getInput('retention-days'))
  });
  const url = `${process.env.GITHUB_SERVER_URL}/${process.env.GITHUB_REPOSITORY}/actions/runs/${process.env.GITHUB_RUN_ID}/artifacts/${result.id}`;
  core.setOutput('artifact-id', result.id);
  core.setOutput('artifact-url', url);
  await core.summary.addLink(`Download ${name}`, url).write();
}

if (process.argv[1] && pathToFileURL(process.argv[1]).href === import.meta.url) {
  main().catch(error => core.setFailed(error.message));
}
