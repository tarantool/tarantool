import os

import requests

# Environments for telegram bot
TOKEN = os.environ['INPUT_TOKEN']
CHAT_ID = os.environ['INPUT_TO']
MESSAGE = os.environ['INPUT_MESSAGE']
PAGE_PREVIEW = os.environ['INPUT_DISABLE_WEB_PAGE_PREVIEW']
BRANCH_NAME = os.environ['GITHUB_REF']
ACTOR = os.environ['GITHUB_ACTOR']

# Static request
REQUEST = f'https://api.telegram.org/bot{TOKEN}/'


def check_branch_name(branch_name):
    branch_list = [
        branch_name != 'refs/heads/master',
        branch_name != 'refs/heads/1.10',
        not branch_name.startswith('refs/heads/2.'),
        not branch_name.startswith('refs/tags'),
        ]

    if any(branch_list):
        return CHAT_ID + "_" + ACTOR
    return CHAT_ID

def send_message(chat_id, message,
                 disable_web_page_preview=False, markdown='MarkdownV2'):
    edit_message = message.replace('-', '\\-').replace('_', '\\_').replace('.', '\\.')

    send_message_url = REQUEST + 'sendMessage'
    send_message_data = {
        'chat_id': chat_id,
        'parse_mode': markdown,
        'disable_web_page_preview': disable_web_page_preview,
        'text': edit_message,
    }

    response = requests.post(send_message_url, send_message_data)
    print(response.json())
    return response.json()


if __name__ == '__main__':
    chat_id = check_branch_name(BRANCH_NAME)
    send_message(chat_id, MESSAGE, PAGE_PREVIEW)
