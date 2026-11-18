const {test} = require('node:test');
const assert = require('node:assert/strict');
const guard = require('../../scripts/ci/runner_queue.js');
function fixture() {
  const outputs = {}, errors = [], cancellations = [];
  const core = {info(){}, error(e){errors.push(e)}, setFailed(e){errors.push(e)}, setOutput(k,v){outputs[k]=v}};
  const context = {repo:{owner:'test',repo:'control'},runId:100};
  const github = {
    rest:{actions:{listSelfHostedRunnersForRepo:'runners',listJobsForWorkflowRunAttempt:'jobs',
      cancelWorkflowRun:async p => {cancellations.push(p)}}},
    paginate: async () => []
  };
  process.env.CI_RUNNER_LABELS = '["self-hosted","macOS"]';
  process.env.CI_EXPECTED_JOBS = '["sdk-build-macos"]';
  process.env.CI_PREPARED_AT = new Date(Date.now()-31*60*1000).toISOString();
  process.env.GITHUB_RUN_ATTEMPT = '2';
  delete process.env.CI_TARGET_REPOSITORY;
  return {core,context,github,outputs,errors,cancellations};
}
test('preflight reports busy online runners and uses target repo', async () => {
  const f=fixture();
  process.env.CI_TARGET_REPOSITORY='test/app';
  f.github.paginate=async (api,p) => {
    assert.equal(p.repo,'app');
    return [{name:'mac',status:'online',busy:true,labels:[{name:'self-hosted'},{name:'macOS'}]}];
  };
  await guard({...f,mode:'preflight'});
  assert.deepEqual(JSON.parse(f.outputs.labels),['self-hosted','macOS']);
});
test('offline matching runner is rejected',async()=>{
  const f=fixture();
  f.github.paginate=async()=>[{name:'mac',status:'offline',busy:false,labels:[{name:'self-hosted'},{name:'macOS'}]}];
  await assert.rejects(guard({...f,mode:'preflight'}),/No matching online/);
});
test('guard queries exact attempt and accepts matching active reused job',async()=>{
  const f=fixture();
  f.github.paginate=async(api,p)=>{
    assert.equal(p.run_id,100); assert.equal(p.attempt_number,2);
    return [{name:'macos / sdk-build-macos',status:'in_progress'}];
  };
  await guard({...f,mode:'guard'});
  assert.equal(f.cancellations.length,0);
});
test('queued job beyond deadline cancels only this run',async()=>{
  const f=fixture();
  f.github.paginate=async()=>[{name:'macos / sdk-build-macos',status:'queued'}];
  await guard({...f,mode:'guard'});
  assert.equal(f.cancellations.length,1);
  assert.equal(f.cancellations[0].run_id,100);
  assert.ok(f.errors.some(e=>/30 minutes/.test(e)));
});
test('ambiguous jobs are never guessed',async()=>{
  const f=fixture();
  f.github.paginate=async()=>[{name:'sdk-build-macos',status:'queued'},{name:'other / sdk-build-macos',status:'in_progress'}];
  await guard({...f,mode:'guard'});
  assert.ok(f.errors.some(e=>/Ambiguous/.test(e)));
  assert.equal(f.cancellations.length,1);
});
test('unrelated platform does not satisfy guard',async()=>{
  const f=fixture();
  f.github.paginate=async()=>[{name:'sdk-build-windows',status:'in_progress'}];
  await guard({...f,mode:'guard'});
  assert.equal(f.cancellations.length,1);
});
