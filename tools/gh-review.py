#!/usr/bin/env python3
"""
Publish a patch review on a GitHub pull request as one single review whose
comments are attached to the diffs of specific commits, like the GitHub web UI
does when reviewing a PR commit by commit.

The public REST API cannot do this: it either binds all comments of a review to
one commit, or publishes each per-commit comment as its own one-comment review.
This tool hides the required internal plumbing.

The plumbing uses quite complicated GraphQL requests because the official REST
API lacks the needed feature:
https://github.com/orgs/community/discussions/168380.

The flow:

    tools/gh-review.py comment --pr <num> ... <params>
    ... more 'comment' invocations ...
    tools/gh-review.py status --pr <num>
    tools/gh-review.py submit --pr <num> -m <review summary>

The first 'comment' invocation creates a pending review, invisible to everyone
else. Each next one appends to it. 'submit' publishes it all as one review.

The 'comment' subcommand also has an opt-in --experimental mode which posts
through the private API of the GitHub web UI. It is more powerful, but requires
cookies for auth.
"""

import argparse
import json
import os
import re
import subprocess
import sys
import traceback
import urllib.error
import urllib.request

API_URL = 'https://api.github.com'
WEB_URL = 'https://github.com'

MUTATION = '''
mutation($rid: ID!, $sha: GitObjectID!, $path: String!, $pos: Int!,
         $body: String!) {
    addPullRequestReviewComment(input: {pullRequestReviewId: $rid,
            commitOID: $sha, path: $path, position: $pos, body: $body}) {
        comment { databaseId }
    }
}
'''

STATUS_QUERY = '''
query($rid: ID!, $cursor: String) {
    node(id: $rid) {
        ... on PullRequestReview {
            comments(first: 100, after: $cursor) {
                pageInfo { hasNextPage endCursor }
                nodes {
                    databaseId
                    body
                    path
                    originalLine
                    originalStartLine
                    originalCommit { abbreviatedOid }
                }
            }
        }
    }
}
'''


def fail(msg):
    print(f'gh-review: {msg}', file=sys.stderr)
    sys.exit(1)


def git(args):
    """Run git and return the exit code, stdout, and stderr. Whether a
    failure is fatal is up to the caller."""
    res = subprocess.run(['git'] + args, capture_output=True,
                         encoding='utf-8')
    return res.returncode, res.stdout, res.stderr


def repo_full_name():
    """The 'owner/repo' pair, parsed from the origin remote URL."""
    code, out, err = git(['remote', 'get-url', 'origin'])
    if code != 0:
        fail(f'cannot read the origin remote URL:\n{err.strip()}')
    url = out.strip()
    m = re.search(r'github\.com[:/](.+?)(?:\.git)?/?$', url)
    if m is None:
        fail(f'cannot parse owner/repo from the origin remote: {url}')
    return m.group(1)


class Api:
    """All the GitHub network access: the public REST and GraphQL APIs and
    the private API of the web UI. Every parameter comes through the
    constructor and nothing is read from the environment here, so the
    tests can aim an instance at a local server."""

    def __init__(self, repo, token, cookie_pair=None, api_url=API_URL,
                 web_url=WEB_URL, timeout=30):
        """The cookie pair is the raw '<login>:<cookie>' value. It is
        stored as is and parsed only when the private API gets used.
        A None token is resolved from the `gh` login on the first
        authenticated request - the private API path works without a
        token at all."""
        self.repo = repo
        self.token = token
        self.cookie_pair = cookie_pair
        self.api_url = api_url
        self.web_url = web_url
        self.timeout = timeout

    def send(self, url, method='GET', headers=None, payload=None):
        """One HTTP request. Returns (status, headers, body): the HTTP
        error statuses are returned, not raised - deciding is up to the
        caller. The network failures are fatal."""
        data = None if payload is None else json.dumps(payload).encode()
        hdrs = dict(headers or {})
        if data is not None:
            hdrs['Content-Type'] = 'application/json'
        req = urllib.request.Request(url, data=data, method=method,
                                     headers=hdrs)
        try:
            with urllib.request.urlopen(req, timeout=self.timeout) as r:
                return r.status, r.headers, r.read().decode('utf-8',
                                                            'replace')
        except urllib.error.HTTPError as e:
            return e.code, e.headers, e.read().decode('utf-8', 'replace')
        except OSError as e:
            fail(f'{method} {url} failed: {e}')

    def auth_headers(self):
        if self.token is None:
            self.token = find_gh_token()
        return {'Accept': 'application/vnd.github+json',
                'Authorization': f'Bearer {self.token}'}

    def rest(self, path, method='GET', payload=None):
        """A public REST API request. Any error status is fatal. Returns
        the parsed JSON body, or None when the body is empty."""
        status, _, body = self.send(f'{self.api_url}/{path}', method,
                                    self.auth_headers(), payload)
        if status >= 400:
            fail(f'{method} /{path} returned HTTP {status}: {body[:200]}')
        return json.loads(body) if body.strip() else None

    def rest_list(self, path):
        """A GET of a whole list. The list endpoints return at most one
        page - 30 items by default, silently - so every page is fetched
        by following the Link response headers."""
        sep = '&' if '?' in path else '?'
        url = f'{self.api_url}/{path}{sep}per_page=100'
        items = []
        while url is not None:
            status, headers, body = self.send(url, 'GET',
                                              self.auth_headers())
            if status >= 400:
                fail(f'GET {url} returned HTTP {status}: {body[:200]}')
            items += json.loads(body)
            m = re.search(r'<([^>]+)>; rel="next"',
                          headers.get('Link') or '')
            url = m.group(1) if m else None
        return items

    def graphql(self, query, variables):
        """A public GraphQL API request. GraphQL reports its errors with
        HTTP 200 and an 'errors' array in the body - both are checked."""
        status, _, body = self.send(f'{self.api_url}/graphql', 'POST',
                                    self.auth_headers(),
                                    {'query': query,
                                     'variables': variables})
        if status >= 400:
            fail(f'GraphQL returned HTTP {status}: {body[:200]}')
        res = json.loads(body)
        if res.get('errors'):
            fail(f'GraphQL failed: {json.dumps(res["errors"])[:300]}')
        return res['data']

    def cookie(self):
        """The session cookie for the private API, extracted from the
        '<login>:<cookie>' pair. The account name is kept inside the
        value itself, because there is no way to find out
        programmatically which account a cookie belongs to, and posting
        with a cookie of one account and a token of another silently
        splits the review in two."""
        if not self.cookie_pair:
            fail('--experimental requires the session cookie in the '
                 'GH_COOKIE environment variable, see '
                 'doc/agents/setup-dev.md')
        login, sep, cookie = self.cookie_pair.partition(':')
        if not sep or not login or not cookie:
            fail('GH_COOKIE must have the <login>:<cookie> format, where '
                 '<login> is the GitHub account name the cookie belongs '
                 'to')
        return cookie


def find_pending_review(api, pr):
    """Pending reviews are visible only to their author, so a PENDING entry in
    the list can only be ours."""
    for review in api.rest_list(f'repos/{api.repo}/pulls/{pr}/reviews'):
        if review['state'] == 'PENDING':
            return review
    return None


def require_pending_review(api, pr):
    review = find_pending_review(api, pr)
    if review is None:
        fail(f'no pending review on PR #{pr}')
    return review


def patch_position(patch, line, is_left):
    """Find the diff position of a file line in one file's patch. The line is
    numbered by the new file version, or by the old one when is_left is set.
    Returns None when the line is not in the patch."""
    pos = None
    old_ln = new_ln = 0
    for text in patch.splitlines():
        header = re.match(r'@@ -(\d+)(?:,\d+)? \+(\d+)(?:,\d+)? @@', text)
        if header:
            pos = 0 if pos is None else pos + 1
            old_ln = int(header.group(1))
            new_ln = int(header.group(2))
            continue
        pos += 1
        tag = text[:1]
        if tag == '+':
            if not is_left and new_ln == line:
                return pos
            new_ln += 1
        elif tag == '-':
            if is_left and old_ln == line:
                return pos
            old_ln += 1
        elif tag != '\\':
            if (old_ln if is_left else new_ln) == line:
                return pos
            old_ln += 1
            new_ln += 1
    return None


def commit_position(base, commit, path, line, is_left):
    """Turn a file line number into a legacy diff 'position'.

    The GraphQL mutation used to attach per-commit comments to a pending review
    predates the modern line+side comment addressing and only accepts the
    archaic 'position': an offset inside the unified diff text of one file. The
    line right below the file's first @@ hunk header is position 1, and the
    offset keeps growing through every following patch line - context lines,
    +/- lines, and further @@ headers alike.

    Moreover, GitHub does not anchor a per-commit comment to the diff of the
    commit against its parent, the way 'git show' and the PR "Commits" tab
    present it.

    The anchor diff is taken between the PR base and the commit, i.e. it
    includes all the PR commits up to this one, which changes the hunks, the
    positions, and the old-side line numbers.

    Hence the file line number taken by this function means the file as of the
    commit for the right side, or as of the PR base for the left side. The
    mapping walks that base-to-commit diff, advancing the position together with
    both file line counters until the requested line is met on the requested
    side.

    The API is most likely so complicated, because GitHub PRs usually are
    reviewed by their whole diff, not commit-per-commit, and the usage here is
    actually quite unnatural to most."""
    sha = resolve_commit(commit)
    base_sha = resolve_commit(base['sha'],
                              f'fetch the base branch first: '
                              f'git fetch origin {base["ref"]}')
    patch = file_patch([f'{base_sha}...{sha}'], path,
                       f'as of commit {commit}')
    pos = patch_position(patch, line, is_left)
    if pos is None:
        side = 'base' if is_left else 'new'
        fail(f'{side}-file line {line} of {path} is not in the PR diff as '
             f'of commit {commit}. Lines both added and deleted between '
             f'the PR base and this commit are not in this diff - anchor '
             f'the comment at a nearby surviving line instead, or use '
             f'--experimental')
    return sha, pos


def resolve_commit(commit, hint='fetch the PR first: '
                                'git fetch origin pull/<pr>/head'):
    """The full SHA of the given commit-ish, from the local repo."""
    code, out, _ = git(['rev-parse', '--verify', '--quiet',
                        commit + '^{commit}'])
    if code != 0:
        fail(f'commit {commit} is not in the local repo - {hint}')
    return out.strip()


def file_patch(refs, path, where):
    """The diff of one file between the given revisions, taken from the local
    git repo and cut down to the first hunk header - the format
    patch_position() expects, and the one the GitHub API serves.

    The GitHub-like settings are enforced so local git setup doesn't mess with
    the results."""
    code, diff, err = git(['diff', '-U3', '--no-color', '--no-ext-diff',
                           '--no-textconv', '--diff-algorithm=myers'] +
                          refs + ['--', path])
    if code != 0:
        fail(f"'git diff' failed:\n{err.strip()}")
    if not diff:
        code, names, err = git(['diff', '--name-only'] + refs)
        if code != 0:
            fail(f"'git diff --name-only' failed:\n{err.strip()}")
        fail(f'file {path} is not changed {where}. Changed files:\n  '
             + '\n  '.join(names.splitlines()))
    idx = diff.find('\n@@')
    if idx < 0:
        fail(f'file {path} has no textual diff {where}')
    return diff[idx + 1:]


def commit_info(commit, path):
    """The commit's own diff against its parent: the full commit SHA, the
    parent SHA, and the patch of the given file. Everything comes from the
    local git repo - the PR must be fetched beforehand (a plain fetch, no
    checkout is needed)."""
    sha = resolve_commit(commit)
    code, out, err = git(['rev-list', '--parents', '-n', '1', sha])
    if code != 0:
        fail(f"'git rev-list' failed:\n{err.strip()}")
    parents = out.split()[1:]
    if len(parents) != 1:
        fail(f'commit {commit} has {len(parents)} parents, need exactly 1')
    return sha, parents[0], file_patch([parents[0], sha], path,
                                       f'in commit {commit}')


def private_comment(api, pr, commit, parent, path, line, start_line,
                    is_left, body):
    """Post a pending review comment via the private endpoint of the GitHub web
    UI. Unlike the public API, it takes an explicit comparison range, so the
    comment is anchored to the commit's own diff against its parent, and can
    address any line of it, or a range of lines ending at `line` when
    `start_line` is given.

    The format of the request is deducted from a real one scrapped from a
    browser."""
    cookie = api.cookie()
    url = (f'{api.web_url}/{api.repo}/pull/{pr}'
           '/page_data/create_review_comment')
    side = 'left' if is_left else 'right'
    anchor = parent if is_left else commit
    positioning = {
        'baseCommitOid': parent,
        'headCommitOid': commit,
    }
    if start_line is None:
        subject = {'subjectType': 'line'}
        positioning.update({
            'type': 'line',
            'path': path,
            'line': line,
            'commitOid': anchor,
        })
    else:
        subject = {
            'subjectType': 'multiline',
            'startLine': start_line,
            'startSide': side,
        }
        positioning.update({
            'type': 'multiline',
            'startPath': path,
            'startLine': start_line,
            'startCommitOid': anchor,
            'endPath': path,
            'endLine': line,
            'endCommitOid': anchor,
        })
    payload = {
        'comparisonStartOid': parent,
        'comparisonEndOid': commit,
        'text': body,
        'submitBatch': False,
        'line': line,
        'path': path,
        'positioning': positioning,
        'side': side,
        **subject,
    }
    status, headers, resp_body = api.send(url, 'POST', {
        'Accept': 'application/json',
        'Origin': api.web_url,
        'Referer': f'{api.web_url}/{api.repo}/pull/{pr}',
        'X-Requested-With': 'XMLHttpRequest',
        'GitHub-Verified-Fetch': 'true',
        'Cookie': cookie,
    }, payload)
    if status >= 400:
        fail(f'the private API returned HTTP {status}: {resp_body[:200]}')
    if 'json' not in headers.get('Content-Type', ''):
        fail('the private API did not return JSON - most likely the '
             'session cookie has expired and needs a refresh, see '
             'doc/agents/local_setup.md')


def read_message(args):
    if args.message is not None:
        return args.message
    try:
        with open(args.message_file, encoding='utf-8') as f:
            return f.read()
    except OSError as e:
        fail(f'cannot read the message file: {e}')


def ensure_pending_review(api, pr):
    review = find_pending_review(api, pr)
    if review is None:
        review = api.rest(f'repos/{api.repo}/pulls/{pr}/reviews', 'POST',
                          {})
        print(f'Started pending review {review["id"]} on PR #{pr}')
    return review


def comment_public(api, args):
    pr = args.pr
    if args.start_line is not None:
        fail('range comments are only possible with --experimental; in '
             'the public mode anchor at one line and describe the range '
             'in the text')
    base = api.rest(f'repos/{api.repo}/pulls/{pr}')['base']
    sha, pos = commit_position(base, args.commit, args.path, args.line,
                               args.left)
    body = read_message(args)
    review = ensure_pending_review(api, pr)
    out = api.graphql(MUTATION, {'rid': review['node_id'], 'sha': sha,
                                 'path': args.path, 'pos': pos,
                                 'body': body})
    cid = out['addPullRequestReviewComment']['comment']['databaseId']
    print(f'Added pending comment {cid} on {sha[:12]} '
          f'{args.path}:{args.line}')


def comment_experimental(api, args):
    pr = args.pr
    sha, parent, patch = commit_info(args.commit, args.path)
    side = 'old' if args.left else 'new'
    if patch_position(patch, args.line, args.left) is None:
        fail(f'{side}-file line {args.line} of {args.path} is not in '
             f'the diff of commit {args.commit}')
    if args.start_line is not None:
        if args.start_line >= args.line:
            fail('--start-line must be less than --line')
        if patch_position(patch, args.start_line, args.left) is None:
            fail(f'{side}-file line {args.start_line} of {args.path} is '
                 f'not in the diff of commit {args.commit}')
    body = read_message(args)
    # No ensure_pending_review() here: the private API attaches the
    # comment to the session's pending review, auto-creating it when
    # there is none - the same way the first comment in the web UI
    # works. It also makes an identity mismatch between the cookie and
    # the token fail loudly later: submit finds no pending review at
    # all, instead of publishing an empty one.
    private_comment(api, pr, sha, parent, args.path, args.line,
                    args.start_line, args.left, body)
    span = (f'{args.line}' if args.start_line is None else
            f'{args.start_line}-{args.line}')
    print(f'Added pending comment on {sha[:12]} '
          f'{args.path}:{span} via the private API')


def cmd_comment(api, args):
    if args.experimental:
        comment_experimental(api, args)
    else:
        comment_public(api, args)


def pending_review_comments(api, review):
    """All comments of the pending review, via GraphQL - the only API
    which exposes the real file line numbers before the review is
    submitted. The REST view of a pending review carries only the legacy
    diff positions."""
    comments = []
    cursor = None
    while True:
        data = api.graphql(STATUS_QUERY, {'rid': review['node_id'],
                                          'cursor': cursor})
        page = data['node']['comments']
        comments += page['nodes']
        if not page['pageInfo']['hasNextPage']:
            return comments
        cursor = page['pageInfo']['endCursor']


def cmd_status(api, args):
    pr = args.pr
    review = find_pending_review(api, pr)
    if review is None:
        print(f'No pending review on PR #{pr}')
        return
    comments = pending_review_comments(api, review)
    print(f'Pending review {review["id"]} on PR #{pr}, '
          f'{len(comments)} comment(s):')
    for c in comments:
        first_line = c['body'].splitlines()[0] if c['body'] else ''
        span = (f'{c["originalLine"]}' if c['originalStartLine'] is None
                else f'{c["originalStartLine"]}-{c["originalLine"]}')
        print(f'  {c["databaseId"]} '
              f'{c["originalCommit"]["abbreviatedOid"]} '
              f'{c["path"]}:{span} | {first_line}')


def cmd_submit(api, args):
    pr = args.pr
    review = require_pending_review(api, pr)
    api.rest(f'repos/{api.repo}/pulls/{pr}/reviews/{review["id"]}/events',
             'POST', {'event': 'COMMENT', 'body': read_message(args)})
    print(f'Submitted review {review["id"]} on PR #{pr}')


def cmd_abort(api, args):
    pr = args.pr
    review = require_pending_review(api, pr)
    api.rest(f'repos/{api.repo}/pulls/{pr}/reviews/{review["id"]}',
             'DELETE')
    print(f'Dropped pending review {review["id"]} on PR #{pr}')


def add_message_args(parser, subject):
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument('-m', '--message', help=f'{subject} text')
    group.add_argument('-F', '--message-file',
                       help=f'read the {subject} text from a file')


def find_gh_token():
    """The token of the user's `gh` login - the fallback for when there
    is no GH_TOKEN in the environment, so the tool keeps working
    without any setup for the default profile."""
    res = subprocess.run(['gh', 'auth', 'token'], capture_output=True,
                         encoding='utf-8')
    if res.returncode != 0:
        fail('no GH_TOKEN in the environment and no `gh` login to take '
             'the token from')
    return res.stdout.strip()


def main():
    # The comments and the GitHub data are UTF-8 regardless of the local
    # environment, so make the output independent from the locale too.
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    sys.stderr.reconfigure(encoding='utf-8', errors='replace')
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    subs = parser.add_subparsers(dest='command', required=True)

    sub = subs.add_parser(
        'comment', help='add a comment to the pending review, creating '
                        'the review if needed')
    sub.add_argument('-c', '--commit', required=True,
                     help='SHA of the commit whose diff the comment is '
                          'attached to')
    sub.add_argument('-p', '--path', required=True,
                     help='path of the commented file')
    sub.add_argument('-l', '--line', required=True, type=int,
                     help='line number in the file as of the commit; '
                          'with --left - in the PR base file, or in the '
                          'parent commit file with --experimental')
    sub.add_argument('--left', action='store_true',
                     help='the line is in the old file version (use for '
                          'deleted lines)')
    sub.add_argument('--start-line', type=int,
                     help='first line of a multi-line comment ending at '
                          '--line; only with --experimental')
    sub.add_argument('--experimental', action='store_true',
                     help='post via the private web UI API: the lines '
                          'are then numbered by the commit own diff, '
                          'and lines added+deleted within the PR become '
                          'reachable; requires GH_COOKIE')
    add_message_args(sub, 'comment')
    sub.set_defaults(func=cmd_comment)

    sub = subs.add_parser('status', help='show the pending review')
    sub.set_defaults(func=cmd_status)

    sub = subs.add_parser(
        'submit', help='publish the pending review with a summary message')
    add_message_args(sub, 'review summary')
    sub.set_defaults(func=cmd_submit)

    sub = subs.add_parser(
        'abort', help='drop the pending review and all its comments')
    sub.set_defaults(func=cmd_abort)

    for sub in subs.choices.values():
        sub.add_argument('--pr', type=int, required=True,
                         help='PR number')

    args = parser.parse_args()
    # The environment is read only here: everything below takes the
    # values as parameters, so the tests can inject their own.
    api = Api(repo=repo_full_name(),
              token=os.environ.get('GH_TOKEN'),
              cookie_pair=os.environ.get('GH_COOKIE'),
              api_url=os.environ.get('GH_API_URL', API_URL),
              web_url=os.environ.get('GH_WEB_URL', WEB_URL))
    args.func(api, args)


if __name__ == '__main__':
    try:
        main()
    except Exception:
        fail(f'internal error:\n{traceback.format_exc()}')
