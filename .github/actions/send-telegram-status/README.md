# Send Telegram notifications with release branches statuses

Action sends notifications to Telegram channel/bot. It checks for the issues the latest commits on release branches and sends the Telegram message with the list of non-success workflows. Action code is written in Python to be able to use ['composite'](https://docs.github.com/en/actions/creating-actions/creating-a-composite-run-steps-action) Github Action type, which can be used independently from different Github Actions workflows. Also use of Python gives the ability to run the Action on any OS with Python installed. Action uses ['MarkdownV2'](https://core.telegram.org/bots/api#markdownv2-style) style for sending messages.

Action sends message to single common `TELEGRAM_TO` Telegram channel for release Tarantool branches:

- 1.10
- 2.\*
- master

## How to use Github Action from Github workflow

Add the following code to the running steps:
```
  - name: call action to send Telegram message on failure
    env:
      TELEGRAM_TOKEN: ${{ secrets.TELEGRAM_CORE_TOKEN }}
      TELEGRAM_TO: ${{ secrets.TELEGRAM_CORE_TO }}
    uses: ./.github/actions/send-telegram-status
    if: failure()
```
