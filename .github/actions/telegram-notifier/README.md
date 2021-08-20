#Send Telegram notifications on failures

Action sends notifications to Telegram channel/bot. Action code is written in Python to be able to use docker container. 
Github Action type, which can be used independently from different Github Actions workflows. 
Also use of Python gives the ability to run the Action on any OS with Python installed. Action uses 'MarkdownV2' style for sending messages. Action has comment in Telegram message that it runs for failed jobs. Github environment variables printing also added to get more information from results log.

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
          message: "🔴 Working testing failed:\n
          *Job*: [ ${{ github.job }} ](https://github.com/${{ github.repository }}/actions/runs/${{ github.run_id }})\n
          *Logs*: [ ${{ env.JOB_ID }} ](https://github.com/${{ github.repository }}/runs/${{ env.JOB_ID }})\n
          *Commit*: [ ${{ github.sha }} ](https://github.com/${{ github.repository }}/commit/${{ github.sha }})\n
          *Branch*: [ ${{ env.BRANCH_NAME }} ](https://github.com/${{ github.repository }}/tree/${{ env.BRANCH_NAME }})\n
          *History*: [ commits ](https://github.com/${{ github.repository }}/commits/${{ github.sha }})\n
          *Triggered on*: ${{ github.event_name }}\n
          *Committer*: ${{ github.actor }}\n
          ---------------- Commit message -------------------\n
          *${{ github.event.head_commit.message }}*\n
          ---------------- Commit message -------------------\n
          "
          disable_web_page_preview: true
 ```

## Secrets

Getting started with Telegram Bot API.

- token: Telegram authorization token.
- to: Unique identifier for this chat.