#Send Telegram notifications on failures

**Action sends notifications to Telegram channel/bot. Action code is written in TypeScript**

Action sends message to single common TELEGRAM_TO Telegram channel for release Tarantool branches and tagged commits:

- 1.10
- 2.*
- master
- 'tagged versions' 
  
And sends all the others to <TELEGRAM_TO>_<USER>.


## How to use Github Action from Github workflow
```yaml
name: Test Workflow Telegram Bot
on: [push]
jobs:
  telegram-bot-test:
    runs-on: ubuntu-latest
    strategy:
      fail-fast: false
    steps:
      - name: Checkout code
        uses: actions/checkout@v2
      - name: Check Commit SHA
        uses: ./.github/actions/commit-hash-check
        with:
          commit: '123456'
      - name: Add-ons for Telegram action
        uses: ./.github/actions/configure_github_envs
        if: always()
      - name: Sending Message to Telegram Chat
        uses: ./.github/actions/telegram-notifier
        if: failure()
        with:
          token: ${{ secrets.TELEGRAM_TOKEN }}
          to: ${{ secrets.TELEGRAM_TO }}
          github_token: ${{ secrets.GITHUB_TOKEN }}
          disable_web_page_preview: true
          parse_mode: MarkdownV2
 ```

## Secrets

Getting started with Telegram Bot API.

- token: Telegram authorization token.
- to: Unique identifier for this chat.
- github_token: Needed to make requests to get additional information for correct message style