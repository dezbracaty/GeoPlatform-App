// Standard GitHub Actions runner preflight and bounded queue observation.
module.exports = async ({github, context, core, mode = process.env.CI_RUNNER_MODE}) => {
  const labels = JSON.parse(process.env.CI_RUNNER_LABELS || '[]');
  const repository = process.env.CI_TARGET_REPOSITORY || `${context.repo.owner}/${context.repo.repo}`;
  const [owner, repo, extra] = repository.split('/');
  if (!owner || !repo || extra || !labels.length || labels.some(x => typeof x !== 'string' || !x))
    throw new Error('Repository and nonempty JSON runner labels are required');
  if (mode === 'preflight') {
    const runners = await github.paginate(github.rest.actions.listSelfHostedRunnersForRepo,
      {owner, repo, per_page: 100});
    const matches = runners.filter(r => labels.every(l => r.labels.some(x => x.name.toLowerCase() === l.toLowerCase())));
    const online = matches.filter(r => r.status === 'online');
    core.info(JSON.stringify(matches.map(r => ({name:r.name, status:r.status, busy:r.busy}))));
    if (!online.length) throw new Error('No matching online repository runner');
    core.setOutput('labels', JSON.stringify(labels));
    return;
  }
  const run_id = context.runId;
  const attempt_number = Number(process.env.GITHUB_RUN_ATTEMPT);
  const expected = JSON.parse(process.env.CI_EXPECTED_JOBS || '[]');
  if (!expected.length) throw new Error('Queue guard needs exact expected jobs');
  const prepared = Date.parse(process.env.CI_PREPARED_AT || '');
  if (!Number.isFinite(prepared)) throw new Error('Queue guard needs prepare completion time');
  const deadline = prepared + 30 * 60 * 1000;
  try {
    while (true) {
      const jobs = await github.paginate(github.rest.actions.listJobsForWorkflowRunAttempt,
        {owner:context.repo.owner, repo:context.repo.repo, run_id, attempt_number, per_page:100});
      const ready = expected.every(name => {
        const found = jobs.filter(j => j.name === name || j.name.endsWith(' / ' + name));
        if (found.length > 1) throw new Error('Ambiguous build job: ' + name);
        return found.length === 1 && (found[0].status === 'in_progress' || found[0].status === 'completed');
      });
      if (ready) return;
      if (Date.now() >= deadline) throw new Error('Build jobs did not start within 30 minutes');
      await new Promise(resolve => setTimeout(resolve, 15000));
    }
  } catch (error) {
    core.setFailed(error.message);
    core.error('Cancelling this run due to queue observation failure; cancellation is not yet confirmed.');
    await github.rest.actions.cancelWorkflowRun({owner:context.repo.owner, repo:context.repo.repo, run_id});
  }
};
