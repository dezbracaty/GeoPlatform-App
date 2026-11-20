"""Exercise the installer's frozen source inputs and checkout boundary."""
import os
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

from test_ci_workflows import load

APP = Path(__file__).resolve().parents[2]


class WorkflowInputChecks(unittest.TestCase):
    def test_installer_freezes_host_and_app_without_reading_sdk_or_test_assets(self):
        workflow = load(APP / '.github/workflows/build-installer.yml')
        source = next(step for step in workflow['jobs']['prepare']['steps'] if step.get('id') == 'source')
        app, host, declared = 'a' * 40, 'b' * 40, 'c' * 40
        requests = {
            '/repos/org/App/branches/feature%2Fapp': {'commit': {'sha': app}},
            f'/repos/org/Host/commits/{host}': {'sha': host},
            f'/repos/org/Host/contents/App?ref={host}': {
                'sha': declared, 'submodule_git_url': 'https://github.com/org/App.git'},
        }
        calls, outputs = [], {}

        class API:
            def request(_, method, path):
                self.assertEqual(method, 'GET')
                calls.append(path)
                return 200, requests[path]

        with (patch.dict(os.environ, GPLATFORM_SOURCE_TOKEN='fixture', GITHUB_REPOSITORY='org/App',
                         APP_REF='feature/app', HOST_REPOSITORY='org/Host', HOST_REF=host),
              patch('common.API', return_value=API()), patch('common.write_outputs', side_effect=outputs.update),
              patch.object(sys, 'path', sys.path[:])):
            exec(compile(source['run'], '<installer-prepare>', 'exec'), {})
        self.assertEqual(calls, list(requests))
        self.assertEqual(outputs, dict(app_sha=app, source_sha=host, declared_app_sha=declared,
                                       host_repository='org/Host', app_repository='org/App'))

    def test_public_installer_checks_out_only_control_and_frozen_host(self):
        path = APP / '.github/workflows/build-installer.yml'
        workflow = load(path)
        self.assertEqual(set(workflow['on']['workflow_dispatch']['inputs']), {'app_ref', 'host_ref', 'platform'})
        build = workflow['jobs']['build']
        checkouts = [step['with'] for step in build['steps'] if step.get('uses', '').startswith('actions/checkout@')]
        self.assertEqual([checkout['path'] for checkout in checkouts], ['control', 'host-source'])
        self.assertEqual(checkouts[1]['repository'], '${{ needs.prepare.outputs.host_repository }}')
        self.assertEqual(checkouts[1]['ref'], '${{ needs.prepare.outputs.source_sha }}')
        package = next(step['run'] for step in build['steps'] if step.get('id') == 'package')
        self.assertIn("'--source-dir', 'host-source'", package)
        self.assertIn("'--source-sha', os.environ['SOURCE_SHA']", package)
        self.assertIn("'--app-sha', os.environ['APP_SHA']", package)
        for removed in ('--sdk-root', '--sdk-sha', '--run-journal', '--test-assets-sha', 'publish_release.py'):
            self.assertNotIn(removed, path.read_text())
