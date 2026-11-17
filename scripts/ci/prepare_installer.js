// Resolve exactly the requested platforms once. Never fetch TestAssets for packaging.
module.exports = async ({github, context, core}) => {
  const platforms = process.env.PLATFORM === 'all' ? ['windows', 'macos'] : [process.env.PLATFORM];
  if (platforms.some(p => !['windows', 'macos'].includes(p))) throw new Error('Invalid installer platform');
  async function resolve(repository, ref) {
    if (!ref || ref.startsWith('-') || ref.startsWith('refs/')) throw new Error('Expected branch or complete SHA');
    const {data} = /^[0-9a-f]{40}$/.test(ref)
      ? await github.rest.repos.getCommit({...repository, ref})
      : await github.rest.repos.getBranch({...repository, branch:ref});
    const sha = data.sha || data.commit.sha;
    if (!/^[0-9a-f]{40}$/.test(sha) || (/^[0-9a-f]{40}$/.test(ref) && sha !== ref))
      throw new Error('Commit did not resolve exactly');
    return sha;
  }
  const app = await resolve(context.repo, process.env.SOURCE_REF);
  const {data:sdk} = await github.rest.repos.getContent({...context.repo, path:'ThirdParty/libs/GPlatformSDK', ref:app});
  const match = /^(?:https:\/\/github\.com\/|git@github\.com:)([^/]+)\/([^/]+?)(?:\.git)?$/.exec(sdk.submodule_git_url || '');
  if (!match || !/^[0-9a-f]{40}$/.test(sdk.sha)) throw new Error('App SDK declaration missing');
  const repository = {owner:match[1], repo:match[2]};
  core.setOutput('app_sha', app);
  core.setOutput('declared_sdk_sha', sdk.sha);
  core.setOutput('sdk_repository', `${repository.owner}/${repository.repo}`);
  const receipt = {app_sha:app, declared_sdk_sha:sdk.sha, sdk_repository:repository, platforms:{}};
  for (const platform of platforms) {
    const ref = process.env[platform.toUpperCase() + '_SDK_REF'] || platform;
    const sha = await resolve(repository, ref);
    core.setOutput(platform + '_sdk_sha', sha);
    receipt.platforms[platform] = {sdk_ref:ref, sdk_sha:sha};
  }
  await core.summary.addHeading('Frozen installer inputs; full Journal not requested')
    .addCodeBlock(JSON.stringify(receipt, null, 2), 'json').write();
};
