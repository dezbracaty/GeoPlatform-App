"""Workflow contracts: selected platforms, skip propagation, and attempt gates."""
import ast
import itertools
import json
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch
import yaml
from fs_fixture import directory_link

ROOT = Path(__file__).resolve().parents[2]
APP = ROOT
sys.path.insert(0, str(ROOT / 'scripts/ci'))


def load(path):
    return yaml.load(path.read_text(), Loader=yaml.BaseLoader)


def sdk_invocations(qt_dir, platform):
    from update_sdk import build_sdk_package
    calls=[]
    env={'BASELINE':'preserve'}
    if qt_dir is not None:env['Qt6_DIR']=qt_dir
    with patch('update_sdk.run',side_effect=lambda args,**kw:calls.append((args,kw))):
        build_sdk_package(Path('/workspace/source with spaces').resolve(),platform,Path('/owned/package').resolve(),env)
    return [(args,kw) for args,kw in calls if '--export' not in args]


def expression(value, needs, platform="all", attempt="1", steps=None, mode="artifacts"):
    condition = value.replace("&&", " and ").replace("||", " or ").replace("!cancelled()", "not cancelled()")
    implicit = not any(f + "(" in value for f in ("cancelled", "always", "success", "failure"))
    if implicit and any(item.result != "success" for item in needs.values()):
        return False
    return bool(eval(condition, {"__builtins__": {}}, {
        "needs": SimpleNamespace(**needs), "inputs": SimpleNamespace(platform=platform, mode=mode),
        "github": SimpleNamespace(run_attempt=attempt),
        "steps": SimpleNamespace(**(steps or {})),
        "cancelled": lambda:False, "always":lambda:True}))


class Workflows(unittest.TestCase):
    def test_regression_build_upload_and_cleanup_share_the_platform_report_root(self):
        job = load(APP / '.github/workflows/run-regression.yml')['jobs']['regression']
        steps = {step.get('name'): step for step in job['steps']}
        build = steps['SDK consumer and complete platform regression']['run']
        upload = steps['Retain regression evidence even on failure']['with']['path']
        cleanup = steps['Clean uploaded report staging']
        self.assertEqual(upload,
            "${{ matrix.platform == 'windows' && runner.temp || github.workspace }}"
            "/regression-results/${{ github.run_id }}-${{ github.run_attempt }}/${{ matrix.platform }}")
        for outcome in ('success', 'failure', 'skipped'):
            self.assertEqual(expression(cleanup['if'], {}, steps={'evidence': SimpleNamespace(outcome=outcome)}),
                             outcome == 'success')
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory).resolve()
            workspace, runner = base / 'workspace', base / 'runner-temp'
            workspace.mkdir()
            runner.mkdir()
            for platform in ('windows', 'macos'):
                with self.subTest(platform=platform):
                    env = dict(os.environ, GITHUB_WORKSPACE=str(workspace), RUNNER_TEMP=str(runner),
                        GITHUB_RUN_ID='123', GITHUB_RUN_ATTEMPT='2', PLATFORM=platform,
                        SOURCE_SHA='a' * 40, APP_SHA='b' * 40, GITHUB_REPOSITORY='owner/App',
                        ASSETS_SHA='c' * 40, ASSETS_REF=platform, SDK_SOURCE='source', SDK_REPOSITORY='owner/SDK')
                    calls = []
                    with patch.dict(os.environ, env), patch.object(subprocess, 'run', side_effect=lambda *a, **k: calls.append(a)):
                        exec(compile(build, '<workflow-build>', 'exec'), {})
                    command = calls[0][0]
                    output = Path(command[command.index('--output') + 1])
                    root = runner if platform == 'windows' else workspace
                    self.assertEqual(output, root / 'regression-results/123-2' / platform)
                    output.mkdir(parents=True)
                    (output / 'report.json').write_text('preserve until uploaded')
                    other = (workspace if platform == 'windows' else runner) / 'unrelated'
                    other.mkdir(exist_ok=True)
                    with patch.dict(os.environ, env):
                        exec(compile(cleanup['run'], '<workflow-cleanup>', 'exec'), {})
                    self.assertFalse(output.exists())
                    self.assertTrue(other.is_dir())

    def test_regression_cleanup_rejects_redirected_platform_report_root(self):
        job = load(APP / '.github/workflows/run-regression.yml')['jobs']['regression']
        cleanup = next(step['run'] for step in job['steps'] if step.get('name') == 'Clean uploaded report staging')
        for platform in ('windows', 'macos'):
            with self.subTest(platform=platform), tempfile.TemporaryDirectory() as directory:
                base = Path(directory).resolve()
                workspace, runner, outside = base / 'workspace', base / 'runner', base / 'outside'
                for root in (workspace, runner, outside):
                    root.mkdir()
                sentinel = outside / 'sentinel'
                sentinel.write_text('keep')
                root = runner if platform == 'windows' else workspace
                directory_link(root / 'regression-results', outside)
                env = dict(os.environ, GITHUB_WORKSPACE=str(workspace), RUNNER_TEMP=str(runner),
                           GITHUB_RUN_ID='123', GITHUB_RUN_ATTEMPT='2', PLATFORM=platform)
                with patch.dict(os.environ, env), self.assertRaisesRegex(RuntimeError, 'redirected report'):
                    exec(compile(cleanup, '<workflow-cleanup>', 'exec'), {})
                self.assertEqual(sentinel.read_text(), 'keep')

    def test_installer_does_not_implicitly_publish_a_release(self):
        workflow = (APP / '.github/workflows/build-installer.yml').read_text()
        for retired in ('publish_release:', 'GPLATFORM_RELEASE_TOKEN', 'publish_release.py'):
            self.assertNotIn(retired, workflow)

    def test_mr_has_only_a_fixed_app_head_reusable_entry_and_no_publication(self):
        flow = load(ROOT / '.github/workflows/pr-checks.yml')
        self.assertEqual(set(flow['on']), {'pull_request'})
        self.assertEqual(set(flow['jobs']), {'regression'})
        job = flow['jobs']['regression']
        self.assertEqual(job['uses'], './.github/workflows/run-regression.yml')
        self.assertEqual(job['with']['app_ref'], '${{ github.event.pull_request.head.sha }}')
        self.assertEqual(job['with']['host_ref'], "${{ vars.GPLATFORM_HOST_REF || 'feature/gplatform-ui' }}")
        self.assertEqual(job['with']['platform'], 'all')
        self.assertEqual(job['with']['sdk_source'], 'source')
        self.assertIn('!github.event.pull_request.draft', job['if'])
        self.assertIn('github.event.pull_request.head.repo.full_name == github.repository', job['if'])
        self.assertNotIn('branches', flow['on']['pull_request'])
        self.assertNotIn('GPLATFORM_PUBLISH_TOKEN', job['secrets'])
        self.assertEqual(job['concurrency']['cancel-in-progress'], 'true')
        self.assertFalse((ROOT / 'scripts/ci/dispatch_app_build.py').exists())

    def test_public_entries_have_only_the_documented_parameters(self):
        expected={
          ROOT/'.github/workflows/build-sdk.yml':{'app_ref','host_ref','platform','mode'},
          ROOT/'.github/workflows/run-regression.yml':{'app_ref','host_ref','platform','sdk_source','macos_sdk_ref','windows_sdk_ref','macos_test_assets_ref','windows_test_assets_ref'},
          APP/'.github/workflows/build-installer.yml':{'app_ref','platform','macos_sdk_ref','windows_sdk_ref'}}
        for path,keys in expected.items():
            self.assertEqual(set(load(path)['on']['workflow_dispatch']['inputs']),keys)

    def test_defaults_choices_and_reusable_inputs_match_the_maintained_document(self):
        document = (APP / 'docs/ci.md').read_text(encoding='utf-8')
        for title, filename in [('Build SDK', 'build-sdk.yml'), ('Run Regression', 'run-regression.yml'),
                                ('Build Installer', 'build-installer.yml')]:
            section = document.split('## ' + title + '\n', 1)[1].split('\n## ', 1)[0]
            fields = {}
            for line in section.splitlines():
                cells = [cell.strip() for cell in line.strip('|').split('|')]
                if len(cells) == 4 and re.fullmatch('[a-z_]+', cells[0]):
                    fields[cells[0]] = cells[1:]
            workflow = load(APP / '.github/workflows' / filename)
            parameters = workflow['on']['workflow_dispatch']['inputs']
            self.assertEqual(set(parameters), set(fields), title)
            for name, parameter in parameters.items():
                declared_type, default, _ = fields[name]
                self.assertEqual(parameter.get('default', ''), '' if default == '空' else default,
                                 title + '/' + name)
                if parameter['type'] == 'choice':
                    self.assertEqual(set(parameter['options']), {part.strip() for part in declared_type.split('/')})
            if 'workflow_call' in workflow['on']:
                reusable = workflow['on']['workflow_call']['inputs']
                self.assertEqual(set(reusable), set(parameters))
                self.assertEqual(reusable['app_ref']['required'], 'true')
                for name in set(parameters) - {'app_ref'}:
                    self.assertEqual(reusable[name].get('default', ''), parameters[name].get('default', ''))

    def test_source_is_retained_and_sdk_prepare_receives_fixed_sha(self):
        for job, expected_path in zip(self.platform_jobs(), ('host-source', 'source')):
            source = next(step for step in job['steps'] if step.get('with', {}).get('path') in ('source', 'host-source'))
            self.assertEqual(source['with']['path'], expected_path)
            self.assertEqual(source['with']['clean'], 'false')
            self.assertEqual(source['with']['persist-credentials'], 'false')
        job = load(ROOT / '.github/workflows/build-sdk.yml')['jobs']['build_publish']
        step = next(step for step in job['steps'] if step.get('name') == 'Build and export selected SDK')
        self.assertEqual(step['env']['SOURCE_SHA'], '${{ needs.prepare.outputs.source_sha }}')

    def test_windows_sdk_invocation_preserves_cmake_arguments_as_environment_data(self):
        for qt_dir in (None, 'C:/Qt SDK/6.9.3/msvc2022_64/lib/cmake/Qt6'):
            calls = sdk_invocations(qt_dir, 'windows')
            self.assertEqual(len(calls), 1)
            args, kwargs = calls[0]
            self.assertEqual(args[:3], ['pwsh', '-NoProfile', '-Command'])
            self.assertEqual(kwargs['cwd'], Path('/workspace/source with spaces').resolve())
            self.assertEqual(kwargs['env']['BASELINE'], 'preserve')
            self.assertEqual(kwargs['env']['CI_BUILD_ENTRY'],
                             str(APP / 'scripts/build.ps1'))
            self.assertEqual(kwargs['env']['CI_SOURCE_DIR'], str(kwargs['cwd']))
            self.assertIn('-SourceDir $env:CI_SOURCE_DIR', args[-1])
            self.assertIn('-Targets gplatform_sdk_artifacts', args[-1])
            self.assertEqual(json.loads(kwargs['env']['CI_CMAKE_ARGS']),
                             ['-DGPLATFORM_BUILD_TESTS=OFF', '-DGPLATFORM_BUILD_ORCA_NATIVE_PROBES=OFF'] + (['-DQt6_DIR=' + qt_dir] if qt_dir else []))
            self.assertNotIn(str(kwargs['cwd']), args[-1])
            if qt_dir:
                self.assertNotIn(qt_dir, args[-1])

    def test_macos_sdk_configures_both_slots_without_host_tests(self):
        for qt_dir in (None, '/Applications/Qt SDK/6.11.2/macos/lib/cmake/Qt6'):
            calls = sdk_invocations(qt_dir, 'macos')
            self.assertEqual(len(calls), 4)
            for index, preset in enumerate(('arm_release', 'x86_release')):
                args, kwargs = calls[index * 2]
                source = kwargs['cwd']
                self.assertEqual(args, ['bash', APP / 'scripts/build.sh', '--source-dir', source, '--preset', preset,
                                       '--configure-only', '--', '-DGPLATFORM_BUILD_TESTS=OFF', '-DGPLATFORM_BUILD_ORCA_NATIVE_PROBES=OFF'] +
                                 (['-DQt6_DIR=' + qt_dir] if qt_dir else []))
                self.assertEqual(calls[index * 2 + 1][0],
                                 ['cmake', '--build', '--preset', preset, '--target', 'gplatform_sdk_artifacts'])

    def platform_jobs(self):
        return [load(ROOT / '.github/workflows/build-sdk.yml')['jobs']['build_publish'],
                load(APP / '.github/workflows/build-installer.yml')['jobs']['build']]

    def test_runtime_is_preinstalled_and_upload_failure_preserves_owned_packages(self):
        for root in (ROOT,APP):
            for path in (root/'.github/workflows').glob('*.yml'):
                text=path.read_text()
                for forbidden in ('actions/setup-python@','PSExecutionPolicyPreference','GITHUB_PATH','runtime_windows','runtime_macos'):
                    self.assertNotIn(forbidden,text)
        for job in self.platform_jobs():
            self.assertEqual(job['steps'][0]['id'],'environment')
            self.assertEqual(job['steps'][0]['shell'],'python')
            for step in job['steps']:
                if not step.get('name','').startswith('Clean '):continue
                owner='artifacts' if step['name'].startswith('Clean owned') else 'environment'
                for outcome in ('success','failure','skipped'):
                    self.assertEqual(expression(step['if'],{},steps={owner:SimpleNamespace(outcome=outcome)}),outcome=='success')

    def test_failed_control_checkout_can_be_cleaned_without_control_helper(self):
        for job in self.platform_jobs():
            with tempfile.TemporaryDirectory() as directory:
                workspace = Path(directory)
                for name in ('source', 'control'):
                    objects = workspace / name / '.git/objects'
                    objects.mkdir(parents=True)
                    obj = objects / 'partial-pack'
                    obj.write_bytes(b'partial checkout')
                    obj.chmod(stat.S_IREAD)
                steps = {step.get('name'):step for step in job['steps'] if step.get('name')}
                package = next(step for name,step in steps.items() if name.startswith('Clean owned'))
                env = {key:value for key,value in os.environ.items()
                       if key not in ('SDK_PACKAGE_ROOT', 'APP_PACKAGE_ROOT')}
                env['GITHUB_WORKSPACE'] = str(workspace)
                for step in (package, steps['Clean temporary control checkout']):
                    result = subprocess.run([os.sys.executable, '-c', step['run']], cwd=workspace,
                                            env=env, capture_output=True)
                    self.assertEqual(result.returncode, 0, result.stderr.decode())
                self.assertTrue((workspace / 'source/.git/objects/partial-pack').exists())
                self.assertFalse((workspace / 'control').exists())

    def test_standalone_checkout_cleanup_rejects_symlinks_and_missing_marker(self):
        for job in self.platform_jobs():
            code = next(step['run'] for step in job['steps'] if step.get('name') == 'Clean temporary control checkout')
            for kind in ('symlink', 'unmarked'):
                with tempfile.TemporaryDirectory() as directory:
                    base = Path(directory)
                    workspace = base / 'workspace'
                    workspace.mkdir()
                    outside = base / 'outside'
                    outside.mkdir()
                    sentinel = outside / 'sentinel'
                    sentinel.write_text('preserve')
                    source = workspace / 'control'
                    if kind == 'symlink':
                        directory_link(source, outside)
                    else:
                        source.mkdir()
                        (source / 'sentinel').write_text('preserve')
                    result = subprocess.run([os.sys.executable, '-c', code], cwd=workspace,
                                            env=dict(os.environ, GITHUB_WORKSPACE=str(workspace)), capture_output=True)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertEqual(sentinel.read_text(), 'preserve')
                    self.assertTrue(source.exists())

    def test_platform_checkout_ignores_system_crlf_and_preserves_git_attributes(self):
        cases = [(ROOT / '.github/workflows/build-sdk.yml', 'build_publish'),
                 (APP / '.github/workflows/build-installer.yml', 'build')]
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            upstream = base / 'upstream'
            upstream.mkdir()
            def git(repo, *args, env=None):
                return subprocess.check_output(['git', '-C', str(repo), *args], env=env, stderr=subprocess.PIPE)
            git(upstream, 'init', '-q')
            git(upstream, 'config', 'user.name', 'CI fixture')
            git(upstream, 'config', 'user.email', 'ci@example.invalid')
            git(upstream, 'config', 'core.autocrlf', 'false')
            (upstream / 'source.cpp').write_bytes(b'int value;\n')
            (upstream / 'explicit.txt').write_bytes(b'attribute controlled\n')
            (upstream / '.gitattributes').write_text('explicit.txt text eol=crlf\n')
            git(upstream, 'add', '.')
            git(upstream, 'commit', '-qm', 'Canonical source bytes')
            global_config = base / 'global.config'
            global_config.write_text('[core]\n\tautocrlf = true\n')
            for index, (path, job_name) in enumerate(cases):
                job = load(path)['jobs'][job_name]
                for platform in ('windows', 'macos'):
                    context = SimpleNamespace(platform=platform)
                    settings = {}
                    for key, value in job['env'].items():
                        value = value.replace('${{ github.workspace }}', str(base))
                        if value.startswith('${{'):
                            code = value[3:-2].strip().replace('&&', ' and ').replace('||', ' or ')
                            value = eval(code, {'__builtins__': {}}, {'inputs':context, 'matrix':context})
                        settings[key] = value
                    env = dict(os.environ, GIT_CONFIG_GLOBAL=str(global_config), GIT_CONFIG_NOSYSTEM='1', **settings)
                    checkout = base / f'{index}-{platform}'
                    git(base, 'clone', '--quiet', str(upstream), str(checkout), env=env)
                    self.assertEqual((checkout / 'source.cpp').read_bytes(), b'int value;\n')
                    self.assertEqual((checkout / 'explicit.txt').read_bytes(), b'attribute controlled\r\n')
                    self.assertEqual(git(checkout, 'status', '--porcelain', env=env), b'')
                    backend = subprocess.run(['git', '-C', str(checkout), 'config', '--get', 'http.sslBackend'],
                                             env=env, capture_output=True)
                    if platform == 'windows':
                        self.assertEqual(backend.stdout.strip(), b'openssl')
                    else:
                        self.assertEqual(backend.returncode, 1)
                    self.assertEqual(job['env']['GIT_HTTP_LOW_SPEED_LIMIT'], '1024')
                    self.assertEqual(job['env']['GIT_HTTP_LOW_SPEED_TIME'], '60')
                    steps = job['steps']
                    self.assertEqual(steps[0]['id'], 'environment')
                    for step in steps:
                        if step.get('uses', '').startswith('actions/checkout@'):
                            expected = 20 if step['with']['path'] in ('source','host-source','sdk','selected-sdk') else 5
                            self.assertEqual(int(step['timeout-minutes']), expected)

    def test_windows_ownership_exceptions_are_exact_job_scoped_gitlinks(self):
        job = load(ROOT / '.github/workflows/build-sdk.yml')['jobs']['build_publish']
        expected = {
            'App', 'App/ThirdParty/OrcaSlicer', 'ThirdParty/QmlDock', 'ThirdParty/SmoothUI',
            'ThirdParty/SmoothUI/SmoothUIDock', 'ThirdParty/SmoothUI/SmoothUIDock/KDDockWidgets',
            'ThirdParty/SmoothUI/SmoothUIDock/KDDockWidgets/tests/reference-images',
            'ThirdParty/SmoothUIDock', 'ThirdParty/SmoothUIDock/KDDockWidgets',
            'ThirdParty/SmoothUIDock/KDDockWidgets/tests/reference-images'}
        env = job['env']
        self.assertEqual(env['GIT_CONFIG_COUNT'], "${{ matrix.platform == 'windows' && '13' || '2' }}")
        actual = {env['GIT_CONFIG_VALUE_' + str(i)] for i in range(3, 13)}
        self.assertEqual(actual, {'${{ github.workspace }}/host-source/' + name for name in expected})
        self.assertTrue(all(env['GIT_CONFIG_KEY_' + str(i)] == 'safe.directory' for i in range(3, 13)))
        self.assertTrue(all('*' not in path and '/Users/' not in path for path in actual))
        with tempfile.TemporaryDirectory(prefix='owned CI workspace ') as temporary:
            workspace = Path(temporary)
            child = workspace / 'host-source/ThirdParty/SmoothUI'
            child.mkdir(parents=True)
            clean = dict(os.environ, GIT_CONFIG_GLOBAL=os.devnull, GIT_CONFIG_NOSYSTEM='1', GIT_CONFIG_COUNT='0')
            subprocess.run(['git', '-C', str(child), 'init', '-q'], env=clean, check=True)
            (child / 'input').write_text('pinned')
            subprocess.run(['git', '-C', str(child), 'add', '.'], env=clean, check=True)
            subprocess.run(['git', '-C', str(child), '-c', 'user.name=Fixture', '-c', 'user.email=fixture@example.invalid',
                            'commit', '-qm', 'owned input'], env=clean, check=True)
            suspect = dict(clean, GIT_TEST_ASSUME_DIFFERENT_OWNER='1')
            rejected = subprocess.run(['git', '-C', str(child), 'rev-parse', 'HEAD'], env=suspect, capture_output=True)
            self.assertNotEqual(rejected.returncode, 0)
            self.assertIn(b'dubious ownership', rejected.stderr)
            trusted = dict(suspect)
            for key, value in env.items():
                if key.startswith('GIT_CONFIG_'):
                    trusted[key] = ('13' if key == 'GIT_CONFIG_COUNT' else
                                    value.replace('${{ github.workspace }}', str(workspace)))
            accepted = subprocess.run(['git', '-C', str(child), 'rev-parse', 'HEAD'], env=trusted, capture_output=True)
            self.assertEqual(accepted.returncode, 0, accepted.stderr.decode())
            self.assertFalse((workspace / '.gitconfig').exists())

    def test_regression_reuses_the_same_exact_owned_git_trust_scope(self):
        sdk = load(ROOT / '.github/workflows/build-sdk.yml')['jobs']['build_publish']['env']
        regression = load(ROOT / '.github/workflows/run-regression.yml')
        job = next(job for job in regression['jobs'].values()
                   if 'matrix.platform' in job.get('env', {}).get('GIT_CONFIG_COUNT', ''))
        env = job['env']
        self.assertEqual(env['GIT_CONFIG_COUNT'], "${{ matrix.platform == 'windows' && '13' || '2' }}")
        for index in range(13):
            self.assertEqual(env['GIT_CONFIG_KEY_' + str(index)], sdk['GIT_CONFIG_KEY_' + str(index)])
            self.assertEqual(env['GIT_CONFIG_VALUE_' + str(index)], sdk['GIT_CONFIG_VALUE_' + str(index)])
        self.assertEqual(job['timeout-minutes'], '360')

    def test_exact_four_app_workflows_and_no_hidden_legacy_entry(self):
        files = sorted((ROOT / '.github/workflows').glob('*.yml'))
        self.assertEqual({p.name for p in files},
                         {'build-sdk.yml', 'run-regression.yml', 'build-installer.yml', 'pr-checks.yml'})
        for path in files:
            workflow = load(path)
            allowed = {'pull_request'} if path.name == 'pr-checks.yml' else {'workflow_dispatch', 'workflow_call'}
            self.assertFalse(set(workflow['on']) - allowed)
            self.assertNotIn('continue-on-error', path.read_text())
            self.assertNotIn('control_ref', path.read_text())
            self.assertNotIn('run-build-app.yml', path.read_text())
            self.assertNotIn('dezbracaty/GPlatform/.github/workflows/', path.read_text())

    def test_sdk_artifacts_are_default_and_publishing_requires_an_explicit_mode(self):
        flow = load(ROOT / '.github/workflows/build-sdk.yml')
        self.assertEqual(flow['on']['workflow_dispatch']['inputs']['mode']['default'], 'artifacts')
        self.assertNotIn('workflow_call',flow['on'])
        self.assertEqual(flow['jobs']['build_publish']['concurrency']['group'],'sdk-${{ matrix.platform }}')
        self.assertEqual(flow['jobs']['build_publish']['strategy']['fail-fast'],'false')
        base = next(step for step in flow['jobs']['prepare']['steps'] if step.get('id') == 'base')
        self.assertFalse(expression(base['if'], {}, mode='artifacts'))
        self.assertTrue(expression(base['if'], {}, mode='publish'))
        job = flow['jobs']['build_publish']
        publish = next(step for step in job['steps'] if step.get('id') == 'publish')
        self.assertIn("publish=os.environ['MODE'] == 'publish'", publish['run'])
        self.assertEqual(publish['env']['MODE'], '${{ inputs.mode }}')
        self.assertEqual(publish['env']['GPLATFORM_PUBLISH_TOKEN'],
                         "${{ inputs.mode == 'publish' && secrets.GPLATFORM_PUBLISH_TOKEN || '' }}")
        self.assertEqual(publish['env']['APP_SHA'], '${{ needs.prepare.outputs.app_sha }}')
        self.assertEqual(publish['env']['SDK_REPOSITORY'], '${{ needs.prepare.outputs.sdk_repository }}')
        upload = next(step for step in job['steps'] if step.get('uses', '').startswith('actions/upload-artifact@'))
        self.assertIn('/SDK', upload['with']['path'])
        self.assertNotIn('/App', upload['with']['path'])
        self.assertNotIn('.bundle', upload['with']['path'])
        self.assertNotIn('app_branch', flow['on']['workflow_dispatch']['inputs'])
        self.assertFalse((ROOT / 'scripts/ci/update_app.py').exists())

    def test_partial_rerun_is_rejected_before_platform_or_guard_scheduling(self):
        flows = [(ROOT / ".github/workflows/build-sdk.yml", ("build_publish","queue_guard")),
                 (APP / ".github/workflows/build-installer.yml", ("build","queue_guard"))]
        for path, jobs in flows:
            flow = load(path)
            for name in jobs:
                condition = flow["jobs"][name]["if"]
                needs = {"prepare":SimpleNamespace(result="success", outputs=SimpleNamespace(attempt="1"))}
                self.assertTrue(expression(condition, needs, attempt="1"))
                self.assertFalse(expression(condition, needs, attempt="2"))

    def test_sdk_result_fails_stale_prepare_even_when_other_results_are_success(self):
        flow = load(ROOT / ".github/workflows/build-sdk.yml")
        command = flow["jobs"]["result"]["steps"][0]["run"]
        code = "\n".join(command.splitlines()[1:-1])
        needs = {name:{"result":"success", "outputs":{}} for name in ("prepare","build_publish","queue_guard")}
        needs["prepare"]["outputs"]["attempt"] = "1"
        result = subprocess.run([os.sys.executable, "-c", code], capture_output=True,
                                env=dict(os.environ, RESULTS=json.dumps(needs), CURRENT_ATTEMPT="2"))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Re-run all jobs", result.stderr.decode())

    def test_actions_are_immutable_and_queue_callers_grant_required_permission(self):
        for root in (ROOT, APP):
            for path in (root / ".github/workflows").glob("*.yml"):
                for action in re.findall(r"uses: (actions/[^\s]+)", path.read_text()):
                    self.assertRegex(action, r"@[0-9a-f]{40}$")
        app = load(APP / ".github/workflows/build-installer.yml")
        self.assertEqual(app["jobs"]["build"]["strategy"]["fail-fast"], "false")
        self.assertEqual(app['jobs']['build']['strategy']['matrix'],'${{ fromJSON(needs.prepare.outputs.matrix) }}')

    def test_missing_credentials_fail_before_checkout_or_heavy_build(self):
        flows = [('build-sdk.yml', 'prepare'), ('run-regression.yml', 'prepare')]
        cases = [(ROOT / '.github/workflows' / name, job) for name, job in flows]
        cases.append((APP / '.github/workflows/build-installer.yml', 'prepare'))
        for path, job in cases:
            step = load(path)['jobs'][job]['steps'][0]
            self.assertEqual(step['name'], 'Check CI credentials')
            code = '\n'.join(step['run'].splitlines()[1:-1])
            for configured in ('true', 'false'):
                env = {key:value for key,value in os.environ.items() if not key.startswith('HAS_')}
                env.update({key:configured for key in step['env'] if key.startswith('HAS_')})
                env.update(MODE='artifacts', PLATFORM='windows')
                result = subprocess.run([os.sys.executable, '-c', code], env=env, capture_output=True)
                self.assertEqual(result.returncode == 0, configured == 'true')


if __name__ == "__main__":
    unittest.main()
