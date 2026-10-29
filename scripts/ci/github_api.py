"""Small GitHub REST client used by the standalone App's CI tools."""
import json
import http.client
import re
import os
import time
import urllib.error
import urllib.request


class GitHub:
    def __init__(self, repository, token=None):
        if not re.fullmatch(r'[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+', repository):
            raise ValueError('Repository must be owner/name')
        self.repository = repository
        self.token = token or os.environ.get('SOURCE_TOKEN') or os.environ.get('GITHUB_TOKEN')

    def request(self, method, path, data=None, *, missing=False):
        url = f'https://api.github.com/repos/{self.repository}' + ('/' + path if path else '')
        body = json.dumps(data).encode() if data is not None else None
        headers = {'Accept': 'application/vnd.github+json', 'User-Agent': 'GeoPlatform-App-CI',
                   'Content-Type': 'application/json'}
        if self.token:
            headers['Authorization'] = 'Bearer ' + self.token
        for attempt in range(4):
            try:
                with urllib.request.urlopen(urllib.request.Request(url, body, headers, method=method), timeout=30) as response:
                    value = response.read()
                    return json.loads(value) if value else None
            except urllib.error.HTTPError as error:
                if missing and error.code == 404:
                    return None
                if method != 'GET' or error.code not in (429, 500, 502, 503, 504) or attempt == 3:
                    raise RuntimeError(f'GitHub {method} failed for {self.repository}: HTTP {error.code}') from None
            except (OSError, urllib.error.URLError, http.client.HTTPException, json.JSONDecodeError):
                if method != 'GET' or attempt == 3:
                    raise RuntimeError(f'GitHub {method} connection interrupted for {self.repository}') from None
            time.sleep(2 ** attempt)
