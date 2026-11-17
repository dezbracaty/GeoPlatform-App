"""Execute the maintained installer resolver with independently moving platform branches."""
import json
from pathlib import Path
import shutil
import subprocess
import unittest


@unittest.skipUnless(shutil.which('node'), 'Requires Node for the workflow JavaScript contract')
class WorkflowInputChecks(unittest.TestCase):
    def resolve(self, platform='windows', **refs):
        module = Path(__file__).resolve().parents[2] / 'scripts/ci/prepare_installer.js'
        fixture = r'''const calls = [], outputs = {};
const input = JSON.parse(process.argv[1]);
Object.assign(process.env, {SOURCE_REF:'feature/app', PLATFORM:input.platform,
  WINDOWS_SDK_REF:input.windows || 'windows', MACOS_SDK_REF:input.macos || 'macos'});
const github = {rest:{repos:{
  async getBranch(args) { calls.push(args); return {data:{commit:{sha:(args.repo === 'App' ? 'a' : args.branch === 'windows' ? 'b':'c').repeat(40)}}}; },
  async getCommit(args) { calls.push(args); return {data:{sha:args.ref}}; },
  async getContent(args) {
    calls.push(args);
    if (args.path !== 'ThirdParty/libs/GPlatformSDK') throw Error('Installer accessed assets');
    return {data:{sha:'d'.repeat(40),submodule_git_url:'https://github.com/org/SDK.git'}};
  }
}}};
const summary = {addHeading(){return this},addCodeBlock(){return this},async write(){}};
require(process.argv[2])({github,context:{repo:{owner:'org',repo:'App'}},
  core:{setOutput(k,v){outputs[k]=v},summary}})
.then(() => process.stdout.write(JSON.stringify({calls,outputs})))
.catch(e => {console.error(e.message);process.exitCode=1});'''
        return subprocess.run([shutil.which('node'), '-e', fixture, json.dumps(dict(platform=platform, **refs)), str(module)],
                              text=True, capture_output=True, timeout=15)

    def test_windows_only_never_resolves_macos_or_assets(self):
        completed = self.resolve()
        self.assertEqual(completed.returncode, 0, completed.stderr)
        result = json.loads(completed.stdout)
        self.assertEqual(result['outputs']['app_sha'], 'a'*40)
        self.assertEqual(result['outputs']['windows_sdk_sha'], 'b'*40)
        self.assertNotIn('macos_sdk_sha', result['outputs'])
        self.assertEqual([c.get('branch') for c in result['calls'] if 'branch' in c], ['feature/app', 'windows'])
        self.assertEqual(result['outputs']['declared_sdk_sha'], 'd'*40)

    def test_all_freezes_distinct_platform_shas_from_declared_repository(self):
        completed = self.resolve('all')
        self.assertEqual(completed.returncode, 0, completed.stderr)
        result = json.loads(completed.stdout)
        self.assertEqual(result['outputs']['macos_sdk_sha'], 'c'*40)
        self.assertEqual(result['outputs']['windows_sdk_sha'], 'b'*40)
        self.assertTrue(all(c['repo'] == 'SDK' for c in result['calls'] if c.get('branch') in ['windows','macos']))

    def test_exact_sdk_sha_is_verified_without_resolving_a_branch(self):
        completed = self.resolve('macos', macos='e'*40)
        self.assertEqual(completed.returncode, 0, completed.stderr)
        result = json.loads(completed.stdout)
        self.assertEqual(result['outputs']['macos_sdk_sha'], 'e'*40)
        self.assertIn(dict(owner='org',repo='SDK',ref='e'*40), result['calls'])

    def test_invalid_platform_fails_before_resolving_sources(self):
        completed = self.resolve('linux')
        self.assertNotEqual(completed.returncode, 0)
        self.assertIn('Invalid installer platform', completed.stderr)

    def test_public_installer_has_no_regression_or_release_controls(self):
        workflow = (Path(__file__).resolve().parents[2] / '.github/workflows/build-app.yml').read_text()
        dispatch = workflow.split('permissions:', 1)[0]
        for old in ['run_journal:', 'run_windows_journal:', 'publish_release:', 'test_assets_ref:', 'request_id:']:
            self.assertNotIn(old, dispatch)
        self.assertNotIn('publish_release.py', workflow)
        self.assertIn('--sdk-root', workflow)
        self.assertIn('--no-run-journal', workflow)
        self.assertIn('matrix: ${{ fromJSON(needs.prepare.outputs.matrix) }}', workflow)
