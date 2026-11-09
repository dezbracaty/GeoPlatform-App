"""Execute the workflow's real resolver without requiring test assets in package-only mode."""
import json
from pathlib import Path
import shutil
import subprocess
import textwrap
import unittest


@unittest.skipUnless(shutil.which('node'), 'Requires Node for the workflow JavaScript contract')
class WorkflowInputChecks(unittest.TestCase):
    def resolve(self, run_journal, windows=False):
        workflow = (Path(__file__).resolve().parents[2] / '.github/workflows/build-app.yml').read_text()
        source_step = workflow.split('      - id: source\n', 1)[1].split('      - id: windows\n', 1)[0]
        script = textwrap.dedent(source_step.split('          script: |\n', 1)[1])
        fixture = '''
        (async () => {
          process.env.SOURCE_REF = 'feature/gplatform-ui';
          process.env.TEST_ASSETS_REF = 'missing-on-purpose';
          process.env.RUN_JOURNAL = RUN_JOURNAL;
          process.env.RUN_WINDOWS_JOURNAL = WINDOWS;
          process.env.WINDOWS_TEST_ASSETS_REF = 'windows';
          const calls = [], outputs = {};
          const github = {rest: {repos: {
            async getBranch(args) {
              calls.push(args);
              return {data: {commit: {sha: (args.repo === 'GeoPlatform-App' ? 'a' : 'c').repeat(40)}}};
            },
            async getContent(args) {
              calls.push(args);
              if (args.path === 'ThirdParty/libs/GPlatformSDK')
                return {data: {sha: 'b'.repeat(40), submodule_git_url: 'https://github.com/example/SDK.git'}};
              if (process.env.RUN_JOURNAL !== 'true' && process.env.RUN_WINDOWS_JOURNAL !== 'true') throw Error('Package-only run accessed TestAssets');
              return {data: {sha: 'd'.repeat(40), submodule_git_url: 'https://github.com/example/TestAssets.git'}};
            }
          }}};
          const summary = {addHeading() {return this}, addTable() {return this}, async write() {}};
          const core = {setOutput(name, value) {outputs[name] = value}, summary};
          const AsyncFunction = Object.getPrototypeOf(async function() {}).constructor;
          await new AsyncFunction('github', 'context', 'core', SCRIPT)(github,
            {repo: {owner: 'example', repo: 'GeoPlatform-App'}}, core);
          process.stdout.write(JSON.stringify({calls, outputs}));
        })().catch(error => {console.error(error); process.exitCode = 1});
        '''
        fixture = fixture.replace('= RUN_JOURNAL;', '= ' + json.dumps(str(run_journal).lower()) + ';')
        fixture = fixture.replace('= WINDOWS;', '= ' + json.dumps(str(windows).lower()) + ';')
        fixture = fixture.replace("'core', SCRIPT)", "'core', " + json.dumps(script) + ")")
        completed = subprocess.run([shutil.which('node'), '-'], input=fixture,
                                   text=True, capture_output=True, check=True, timeout=15)
        return json.loads(completed.stdout)

    def test_disabled_regression_never_resolves_test_assets(self):
        result = self.resolve(False)
        self.assertEqual(result['outputs']['app_sha'], 'a' * 40)
        self.assertEqual(result['outputs']['sdk_sha'], 'b' * 40)
        self.assertEqual(result['outputs']['test_assets_sha'], '')
        self.assertEqual(len(result['calls']), 2)

    def test_enabled_regression_still_pins_independent_test_repository(self):
        result = self.resolve(True)
        self.assertEqual(result['outputs']['test_assets_sha'], 'c' * 40)
        self.assertIn(dict(owner='example', repo='TestAssets', branch='missing-on-purpose'), result['calls'])

    def test_windows_only_pins_windows_without_resolving_macmini(self):
        result = self.resolve(False, windows=True)
        self.assertEqual(result['outputs']['test_assets_sha'], '')
        self.assertEqual(result['outputs']['windows_test_assets_sha'], 'c' * 40)
        self.assertIn(dict(owner='example', repo='TestAssets', branch='windows'), result['calls'])
        self.assertFalse(any(c.get('branch') == 'missing-on-purpose' for c in result['calls']))

    def test_both_platforms_resolve_their_own_asset_ref(self):
        result = self.resolve(True, windows=True)
        self.assertEqual(result['outputs']['windows_test_assets_sha'], 'c' * 40)
        self.assertIn(dict(owner='example', repo='TestAssets', branch='windows'), result['calls'])
        self.assertIn(dict(owner='example', repo='TestAssets', branch='missing-on-purpose'), result['calls'])


if __name__ == '__main__':
    unittest.main()
