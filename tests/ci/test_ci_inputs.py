import base64
import json
"""Frozen GitHub source identities and bounded read-only transport; no workflow dispatch."""
from pathlib import Path
import sys
import unittest
from unittest.mock import patch
from http.client import IncompleteRead
from urllib.error import HTTPError
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'scripts/ci'))
from common import freeze_candidate, API, APIError, source_app_sha
SHA='a'*40

class GitHubTransportTests(unittest.TestCase):
    def test_http_truncation_and_invalid_json_become_uncertain_post(self):
        for payload in (b'{"workflow_run_id":', IncompleteRead(b"partial", 99)):
            response = unittest.mock.MagicMock()
            response.__enter__.return_value = response
            response.status = 200
            if isinstance(payload, Exception):
                response.read.side_effect = payload
            else:
                response.read.return_value = payload
            with patch('common.urlopen', return_value=response) as request:
                with self.assertRaisesRegex(RuntimeError, 'response unavailable'):
                    API('test-token').request('POST', '/fixture', {})
                self.assertEqual(request.call_count, 1)

    def test_long_retry_after_fails_without_an_early_retry(self):
        error = HTTPError('https://api.github.com/fixture', 429, 'limited', {'Retry-After': '120'}, None)
        with patch('common.urlopen', side_effect=error) as request, patch('common.time.sleep') as sleep:
            with self.assertRaisesRegex(RuntimeError, 'no early retry'):
                API('test-token').request('GET', '/fixture')
            request.assert_called_once()
            sleep.assert_not_called()


class SourceCandidateTests(unittest.TestCase):
    def test_uses_exact_root_gitlink_without_resolving_a_moving_app_branch(self):
        api = API('fixture')
        with patch.object(api, 'request', return_value=(200, {
                'type': 'submodule', 'sha': 'b' * 40,
                'submodule_git_url': 'https://github.com/example/app.git'})) as request:
            self.assertEqual(source_app_sha(api, 'example/root', SHA, 'example/app'), 'b' * 40)
        request.assert_called_once_with('GET', f'/repos/example/root/contents/App?ref={SHA}')

    def test_missing_directory_wrong_repository_and_malformed_sha_are_rejected(self):
        api = API('fixture')
        for item in ([], {'sha': 'b' * 40},
                     {'sha': 'b' * 40, 'submodule_git_url': 'https://github.com/other/app.git'},
                     {'sha': 'not-a-sha', 'submodule_git_url': 'https://github.com/example/app.git'}):
            with self.subTest(item=item), patch.object(api, 'request', return_value=(200, item)):
                with self.assertRaises((ValueError, RuntimeError)):
                    source_app_sha(api, 'example/root', SHA, 'example/app')

    def test_invalid_candidate_is_rejected_before_network_access(self):
        api = API('fixture')
        with patch.object(api, 'request') as request:
            for repository, sha in [('example/root', 'feature/moving'), ('bad/repo/extra', SHA)]:
                with self.assertRaises(ValueError):
                    source_app_sha(api, repository, sha, 'example/app')
            request.assert_not_called()


class AppCandidateTests(unittest.TestCase):
    def test_sdk_declaration_has_one_repository_and_no_pin_fallback(self):
        from common import parse_sdk_repository, sdk_repository
        for declaration in ({}, {'repository': '../bad'}, {'repository':'org/SDK','sha':'a'*40},
                            {'repository':None}, [], 'not a declaration'):
            with self.subTest(declaration=declaration), self.assertRaisesRegex(RuntimeError, 'declaration'):
                parse_sdk_repository(json.dumps(declaration))
        self.assertEqual(parse_sdk_repository('{"repository":"org/SDK"}'), 'org/SDK')
        api = API('fixture')
        for item in ([], {}, {'type':'submodule','sha':'a'*40,'submodule_git_url':'https://github.com/org/SDK.git'},
                     {'type':'file','encoding':'base64','content':'@bad!'},
                     {'type':'file','encoding':'base64','content':base64.b64encode(b'{}').decode()}):
            with self.subTest(item=item), patch.object(api,'request',return_value=(200,item)) as request:
                with self.assertRaisesRegex(RuntimeError,'declaration'):
                    sdk_repository(api,'org/App','a'*40)
                self.assertEqual(request.call_count,1)

    def test_selected_app_is_independent_of_the_hosts_older_gitlink(self):
        from common import API
        api = API('fixture')
        responses = {
            '/repos/org/App/branches/feature%2Fcurrent': {'commit': {'sha': 'c' * 40}},
            '/repos/org/Host/branches/feature%2Fhost': {'commit': {'sha': 'a' * 40}},
            '/repos/org/Host/contents/App?ref=' + 'a' * 40:
                {'sha': 'b' * 40, 'submodule_git_url': 'https://github.com/org/App.git'},
            '/repos/org/App/contents/cmake/sdk-repository.json?ref=' + 'c' * 40:
                {'type':'file','encoding':'base64','content':base64.b64encode(b'{"repository":"org/SDK"}').decode()},
        }
        with patch.object(api, 'request', side_effect=lambda method, path: (200, responses[path])) as request:
            candidate = freeze_candidate(api, 'org/App', 'feature/current', 'org/Host', 'feature/host')
        self.assertEqual(candidate['app_sha'], 'c' * 40)
        self.assertEqual(candidate['declared_app_sha'], 'b' * 40)
        self.assertEqual(candidate['source_sha'], 'a' * 40)
        self.assertEqual(candidate['sdk_repository'], 'org/SDK')
        self.assertEqual(request.call_count, 4)
        self.assertEqual(sum('/branches/' in call.args[1] for call in request.call_args_list), 2)
        self.assertFalse(any('ref=' + 'b' * 40 in call.args[1] for call in request.call_args_list))

    def test_host_cannot_redirect_the_app_to_another_repository(self):
        from common import API
        api = API('fixture')
        with patch.object(api, 'request', side_effect=[(200, {'sha': 'c' * 40}),
                (200, {'sha': 'a' * 40}), (200, {'sha': 'b' * 40,
                    'submodule_git_url': 'https://github.com/other/App.git'})]):
            with self.assertRaisesRegex(RuntimeError, 'configured App repository'):
                freeze_candidate(api, 'org/App', 'c' * 40, 'org/Host', 'a' * 40)

if __name__=="__main__":unittest.main()
