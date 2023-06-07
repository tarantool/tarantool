# Send VK Teams notification on failure

This action selects the corresponding chat for the notification on failure and
uses [report-job-status action](https://github.com/tarantool/actions/tree/master/report-job-status)
for sending it to:
* the team chat (Tarantool CI/CD reports) - if the job started on any event in
the `master` or release branch;
* a personal chat, created by the committer – if the job started on
creating/updating a pull request or any event in other branches.

__IMPORTANT__: The secrets are [not passed](https://docs.github.com/en/actions/security-guides/encrypted-secrets#using-encrypted-secrets-in-a-workflow)
to workflows that are triggered by a pull request from a fork. Due to
this limitation, this action will not work for pull requests from forks.

## How to create personal chat

1. Create a new public chat in VK Teams (access can be limited by enabling
`Join in Approval` in the chat settings).
2. Change URL of the created chat: use URL of the team chat and add
`_<your_github_nickname>` to it, for example:
`https://vkteams.example.com/profile/team_chat_mynickname`.
3. Add the bot user `tarantoolbot` to your personal chat.

## How to use GitHub action from GitHub workflow

Add the following code to the running steps:
```yml
  - name: Send VK Teams message on failure
    if: failure()
    uses: ./.github/actions/report-job-status
    with:
      bot-token: ${{ secrets.VKTEAMS_BOT_TOKEN }}
```
