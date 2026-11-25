"""Exercise the installer's frozen App/SDK choices and checkout boundary."""
import base64
import os
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

from test_ci_workflows import load
import common

APP = Path(__file__).resolve().parents[2]


class WorkflowInputChecks(unittest.TestCase):
    def test_installer_freezes_app_and_only_the_selected_sdk(self):
        workflow = load(APP / '.github/workflows/build-installer.yml')
        source = next(step for step in workflow['jobs']['prepare']['steps'] if step.get('id') == 'source')
        app, sdk = 'a' * 40, 'b' * 40
        requests = {
            '/repos/org/App/branches/feature%2Fapp': {'commit': {'sha': app}},
            f'/repos/org/App/contents/cmake/sdk-repository.json?ref={app}': {
                'type': 'file', 'encoding': 'base64', 'content': base64.b64encode(b'{"repository":"org/SDK"}').decode()},
            f'/repos/org/SDK/commits/{sdk}': {'sha': sdk},
        }
        calls, outputs = [], {}

        class API:
            def request(_, method, path):
                self.assertEqual(method, 'GET')
                calls.append(path)
                return 200, requests[path]

        with (patch.dict(os.environ, GPLATFORM_SOURCE_TOKEN='fixture', GITHUB_REPOSITORY='org/App',
                         APP_REF='feature/app', PLATFORM='windows', WINDOWS_SDK_REF=sdk, MACOS_SDK_REF='unused'),
              patch('common.API', return_value=API()), patch('common.write_outputs', side_effect=outputs.update),
              patch('common.read_remote_head') as branch, patch.object(sys, 'path', sys.path[:])):
            exec(compile(source['run'], '<installer-prepare>', 'exec'), {})
        branch.assert_not_called()
        self.assertEqual(calls, list(requests))
        self.assertEqual(outputs, dict(app_sha=app, sdk_repository='org/SDK', windows_sdk_sha=sdk))

    def test_public_installer_checks_out_only_control_and_frozen_app_and_sdk(self):
        path = APP / '.github/workflows/build-installer.yml'
        workflow = load(path)
        self.assertEqual(set(workflow['on']['workflow_dispatch']['inputs']),
                         {'app_ref', 'platform', 'windows_sdk_ref', 'macos_sdk_ref'})
        build = workflow['jobs']['build']
        checkouts = [step['with'] for step in build['steps'] if step.get('uses', '').startswith('actions/checkout@')]
        self.assertEqual([checkout['path'] for checkout in checkouts], ['control', 'source', 'sdk'])
        self.assertEqual(checkouts[1]['ref'], '${{ needs.prepare.outputs.app_sha }}')
        self.assertEqual(checkouts[2]['repository'], '${{ needs.prepare.outputs.sdk_repository }}')
        package = next(step['run'] for step in build['steps'] if step.get('id') == 'package')
        self.assertIn("'--source-dir', 'source'", package)
        self.assertIn("'--app-sha', os.environ['APP_SHA']", package)
        self.assertIn("'--sdk-sha', os.environ['SDK_SHA']", package)
        for removed in ('host-source', '--source-sha', '--run-journal', '--test-assets-sha', 'publish_release.py'):
            self.assertNotIn(removed, path.read_text())

    def test_branch_priority_and_fallback_are_shared_and_frozen_once(self):
        heads = {'feature/app': 'a'*40, 'feature/override': 'b'*40, 'windows': 'c'*40, 'macos': 'd'*40}

        def select(app_ref, expected_calls, **kwargs):
            with patch('common.read_remote_head', side_effect=lambda _, name, **kw: heads.get(name, '')) as git:
                result = common.freeze_published_sdk(None, 'org/SDK', app_ref, **kwargs)
            self.assertEqual([call.args[1] for call in git.call_args_list], expected_calls)
            return result

        self.assertEqual(select('feature/app', ['feature/app'], platform='all'),
                         dict(windows_sdk_sha='a'*40, macos_sdk_sha='a'*40))
        self.assertEqual(select('feature/app', ['feature/override', 'feature/app'], platform='all',
                                windows_sdk_ref='feature/override'),
                         dict(windows_sdk_sha='b'*40, macos_sdk_sha='a'*40))
        self.assertEqual(select('feature/absent', ['feature/absent', 'windows', 'macos'], platform='all'),
                         dict(windows_sdk_sha='c'*40, macos_sdk_sha='d'*40))
        self.assertEqual(select('f'*40, ['macos'], platform='macos'), dict(macos_sdk_sha='d'*40))
        self.assertEqual(select('f'*40, ['feature/app'], platform='macos', app_branch='feature/app'),
                         dict(macos_sdk_sha='a'*40))

    def test_explicit_missing_branch_and_lookup_errors_cannot_select_platform_default(self):
        with patch('common.read_remote_head', return_value='') as branch:
            with self.assertRaisesRegex(RuntimeError, 'does not exist for windows'):
                common.freeze_published_sdk(None, 'org/SDK', 'feature/app', platform='windows',
                                            windows_sdk_ref='feature/missing')
            branch.assert_called_once()
            self.assertEqual(branch.call_args.args[1], 'feature/missing')
        with patch('common.read_remote_head', side_effect=RuntimeError('transport failed')) as branch:
            with self.assertRaisesRegex(RuntimeError, 'transport failed'):
                common.freeze_published_sdk(None, 'org/SDK', 'feature/app', platform='windows')
            branch.assert_called_once()
