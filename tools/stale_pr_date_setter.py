#!/usr/bin/env python3
"""
Tarantool Lango team PR last activity date setter.

This script makes use of GitHub GraphQL API to check the activity
on pull requests listed on the specified board and update the
last activity date correspondingly.

Parameters are passed via environment variables.
Available parameters:
    - LANGO_REVIEW_BOARD_TOKEN: GitHub token with read/write rights
    for the specified project and read rights for PRs in that project.
    - ORGANIZATION: Name of the organization that project belongs to.
    - PROJECT_ID: ID of the GitHub project.

NB: This script is suitable only for GitHub ProjectV2.
"""
import json
import logging
import os
import sys
from datetime import datetime, timezone
from enum import Enum

import requests


class Exitcode(Enum):
    ERR_PROJECT = 1
    ERR_FIELD = 2
    ERR_PR = 3
    ERR_PARAMS = 4


logging.basicConfig()
logger = logging.getLogger('review_date_setter')
logger.setLevel(logging.INFO)

try:
    TOKEN = os.environ['LANGO_REVIEW_BOARD_TOKEN']
    ORGANIZATION = os.environ['ORGANIZATION']
    PROJECT_ID = int(os.environ['PROJECT_ID'])
except KeyError:
    logger.critical('No parameters provided')
    sys.exit(Exitcode.ERR_PARAMS)

HEADERS = {'Authorization': 'Bearer {token}'.format(token=TOKEN)}
ITEM_DATE_FIELD_NAME = 'No response since'
URL = 'https://api.github.com/graphql'

PROJECT_ID_REQUEST = """
query
{
    organization(login: "%s")
    {
        projectV2(number: %d)
        {
            id
        }
    }
}
"""

DATEFIELD_ID_REQUEST = """
query
{
    node(id: "%s")
    {
        ... on ProjectV2
        {
            field(name: "%s")
            {
                ... on ProjectV2FieldCommon
                {
                    id
                    name
                }
            }
        }
    }
}
"""

ITEMS_REQUEST = """
query
{
    node(id: "%s")
    {
        ... on ProjectV2
        {
            items(first: 20 after: "%s")
            {
                pageInfo
                {
                    startCursor
                    hasNextPage
                    endCursor
                }

                nodes
                {
                    id
                    fieldValueByName(name: "%s")
                    {
                        ... on ProjectV2ItemFieldDateValue
                        {
                            date
                        }
                    }
                    content
                    {
                        ...on PullRequest
                        {
                            id
                            title
                            closed
                            updatedAt
                        }
                    }
                }
            }
        }
    }
}
"""

DATEFIELD_UPDATE_REQUEST = """
mutation
{
    updateProjectV2ItemFieldValue(
        input:
        {
            projectId: "%s"
            itemId: "%s"
            fieldId: "%s"
            value:
            {
                date: "%s"
            }
        }
    )
    {
        projectV2Item
        {
            id
        }
    }
}
"""


def _table_field_date(item_date_field):
    table_date = datetime.min
    if item_date_field is not None:
        table_date = datetime.fromisoformat(item_date_field['date'])
    return table_date.replace(tzinfo=timezone.utc)


def _gql_request(query):
    try:
        resp = requests.post(
            URL,
            headers=HEADERS,
            json={'query': query},
            timeout=1,
        )
    except requests.exceptions.Timeout:
        logger.error('Connection timed out')
        return None

    if resp.status_code != requests.codes.ok:
        logger.error(resp.text)
        return None

    try:
        gql_data = json.loads(resp.text)
    except json.JSONDecodeError as exc:
        logger.error(
            'Failed to decode GQL response: {error}'.format(error=str(exc)),
        )
        return None

    if 'errors' in gql_data:
        logger.error('Invalid GQL query: {error}'.format(error=gql_data))
        return None

    return gql_data


def _perform_update(project_id, item_id, field_id, date, title):
    logger.info('Updating {title} ...'.format(title=title))
    confirmation = _gql_request(
        DATEFIELD_UPDATE_REQUEST % (project_id, item_id, field_id, date),
    )
    if confirmation is None:
        logger.warning('Failed to update {title}'.format(title=title))
    logger.info(confirmation)


def _get_project_id(organization, project_id):
    project_data = _gql_request(
        PROJECT_ID_REQUEST % (organization, project_id),
    )
    if project_data is None:
        logger.critical('Failed to get project id')
        sys.exit(Exitcode.ERR_PROJECT)

    project_id = project_data['data']['organization']['projectV2']['id']
    logger.info('Got project id {project_id}'.format(project_id=project_id))
    return project_id


def _get_date_field_id(project_id, date_field_name):
    date_field_data = _gql_request(
        DATEFIELD_ID_REQUEST % (project_id, date_field_name),
    )

    if date_field_data is None:
        logger.critical('Failed to get date field id')
        sys.exit(Exitcode.ERR_FIELD)

    date_field_id = date_field_data['data']['node']['field']['id']
    logger.info('Got date field id {project_id}'.format(project_id=project_id))
    return date_field_id


def _get_pr_items(project_id, date_field_name):
    cursor_after = None
    has_next_page = True
    pr_items = []

    while has_next_page:
        project_items_data = _gql_request(
            ITEMS_REQUEST % (project_id, cursor_after, date_field_name),
        )
        if project_items_data is None:
            logger.critical('Failed to get PRs')
            sys.exit(Exitcode.ERR_PR)

        pr_items += project_items_data['data']['node']['items']['nodes']
        page_info = project_items_data['data']['node']['items']['pageInfo']
        cursor_after = page_info['endCursor']
        has_next_page = page_info['hasNextPage']

    return filter(
        # Skip items that are not pull requests or not open.
        lambda entry: len(entry['content']) and not entry['content']['closed'],
        pr_items,
    )


def _update_pr_dates(pr_items, project_id, date_field_id):
    for pr_item in pr_items:
        pull_request = pr_item['content']
        item_id = pr_item['id']
        pr_last_update = datetime.fromisoformat(pull_request['updatedAt'])
        table_last_update = _table_field_date(pr_item['fieldValueByName'])

        if table_last_update < pr_last_update:
            _perform_update(
                project_id,
                item_id,
                date_field_id,
                pr_last_update.isoformat(),
                pull_request['title'],
            )


if __name__ == '__main__':
    project_id = _get_project_id(ORGANIZATION, PROJECT_ID)
    date_field_id = _get_date_field_id(project_id, ITEM_DATE_FIELD_NAME)
    pr_items = _get_pr_items(project_id, ITEM_DATE_FIELD_NAME)
    _update_pr_dates(pr_items, project_id, date_field_id)
