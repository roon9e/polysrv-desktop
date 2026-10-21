#!/usr/bin/env python3
"""
This file is part of PolySRV Desktop,
a Telegram Desktop fork for the PolySRV multi-server client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

Publishes the no-CI branch.

The branch is the tree of `dev` without the GitHub Actions workflows, so that
somebody can clone it and build the client locally without the CI machinery
in their tree. A push to it starts no run, because there is nothing in the
branch that GitHub Actions could start.

The branch holds a single commit, which is replaced on every publish: it is a
snapshot of the code, not a history of it.

Usage:
    python3 publish_no_ci_branch.py [--repo owner/name] [--source dev]
                                    [--target no-ci] [--dry-run]
"""

import argparse
import json
import os
import sys
import urllib.error
import urllib.request

WorkflowsPath = '.github/workflows'


def Api(path, method='GET', body=None, token=None, missing_ok=False):
    url = 'https://api.github.com' + path
    data = None
    headers = {
        'Accept': 'application/vnd.github+json',
        'User-Agent': 'polysrv-publish-no-ci',
    }
    if body is not None:
        data = json.dumps(body).encode('utf-8')
        headers['Content-Type'] = 'application/json'
    if token:
        headers['Authorization'] = 'Bearer ' + token
    request = urllib.request.Request(url, data=data, headers=headers,
                                     method=method)
    try:
        with urllib.request.urlopen(request) as response:
            return json.loads(response.read().decode('utf-8'))
    except urllib.error.HTTPError as error:
        if missing_ok and error.code == 404:
            return None
        detail = error.read().decode('utf-8', 'replace')
        raise SystemExit('%s %s failed: %s %s'
                         % (method, path, error.code, detail))


def main(argv):
    parser = argparse.ArgumentParser()
    parser.add_argument('--repo', default=os.environ.get('GITHUB_REPOSITORY'))
    parser.add_argument('--source', default='dev')
    parser.add_argument('--target', default='no-ci')
    parser.add_argument('--dry-run', action='store_true')
    args = parser.parse_args(argv)

    if not args.repo:
        raise SystemExit('no repository given')

    token = (os.environ.get('GH_TOKEN')
             or os.environ.get('GITHUB_TOKEN'))
    if not token:
        raise SystemExit('no GH_TOKEN or GITHUB_TOKEN in the environment')

    source = Api('/repos/%s/git/ref/heads/%s' % (args.repo, args.source),
                 token=token)
    sourceSha = source['object']['sha']
    sourceCommit = Api('/repos/%s/git/commits/%s'
                       % (args.repo, sourceSha), token=token)
    sourceTree = sourceCommit['tree']['sha']

    tree = Api('/repos/%s/git/trees/%s?recursive=1'
               % (args.repo, sourceTree), token=token)
    if tree.get('truncated'):
        raise SystemExit('the source tree is too large to copy')

    # The new tree is the source tree without the workflow files. Listing the
    # files to remove is far smaller than listing the whole tree, and the
    # tree API removes a file whose sha is null.
    removals = []
    for entry in tree['tree']:
        if entry['type'] == 'blob' and entry['path'].startswith(
                WorkflowsPath + '/'):
            removals.append({
                'path': entry['path'],
                'mode': entry['mode'],
                'type': 'blob',
                'sha': None,
            })

    kept = len([entry for entry in tree['tree']
                 if entry['type'] == 'blob']) - len(removals)
    print('%s: %d files, %d workflow files dropped'
          % (args.source, kept, len(removals)))

    if args.dry_run:
        return 0

    newTree = Api('/repos/%s/git/trees' % args.repo, 'POST', {
        'base_tree': sourceTree,
        'tree': removals,
    }, token=token)

    message = ('PolySRV without CI\n\n'
               'The tree of %s at %s, without the GitHub Actions workflows, so'
               ' that the client can be built locally without them.'
               % (args.source, sourceSha))
    commit = Api('/repos/%s/git/commits' % args.repo, 'POST', {
        'message': message,
        'tree': newTree['sha'],
    }, token=token)

    target = 'refs/heads/' + args.target
    existing = Api('/repos/%s/git/ref/heads/%s'
                   % (args.repo, args.target), token=token, missing_ok=True)
    if existing:
        Api('/repos/%s/git/refs/%s' % (args.repo, target), 'PATCH', {
            'sha': commit['sha'],
            'force': True,
        }, token=token)
        print('%s updated to %s' % (args.target, commit['sha']))
    else:
        Api('/repos/%s/git/refs' % args.repo, 'POST', {
            'ref': target,
            'sha': commit['sha'],
        }, token=token)
        print('%s created at %s' % (args.target, commit['sha']))

    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
