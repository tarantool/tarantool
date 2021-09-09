import * as core from "@actions/core";
import * as github from "@actions/github";
import { PushEvent } from "@octokit/webhooks-definitions/schema";
import axios from "axios";

// Running function that is sending message with the correct specified data.
async function run(): Promise<void> {
  try {
    core.info("Starting process...");
    const botToken: string = core.getInput("token");
    const branchName: string = github.context.ref;

    const chatId: string | number = await getChatId(branchName);
    const parseMode: string = core.getInput("parse_mode");
    const disablePagePreview: boolean = core.getBooleanInput(
      "disable_web_page_preview"
    );

    await sendMessage(botToken, chatId, parseMode, disablePagePreview);
  } catch (error: any) {
    core.setFailed(error.message);
  }
}

/**
 * Sending message to channel.
 * Sends message via Telegram Bot API by POST request
 * to specified chat id with specified message. The request is
 * made by axios (javascript framework).
 *
 * @async
 * @param {string} botToken - Token that gives access to an API.
 * @param {string | number} chatId - Chat where message should be sent.
 * @param {string} parseMode - Way of formatting the message (Markdown, HTML)
 * @param {boolean} disablePagePreview - Displays Page preview if true, else false.
 */
async function sendMessage(
  botToken: string,
  chatId: string | number,
  parseMode: string,
  disablePagePreview: boolean
) {
  const BRANCH_NAME = await getBranchName();
  const JOB_ID = await getJobId();

  const githubData = github.context.payload as PushEvent;

  const message = `🔴 Working testing failed:
  *Job*: [ ${github.context.job} ](https://github.com/${githubData.repository.full_name}/actions/runs/${github.context.runId})
  *Logs*: [ #${JOB_ID} ](https://github.com/${githubData.repository.full_name}/runs/${JOB_ID})
  *Commit*: [ ${github.context.sha} ](https://github.com/${githubData.repository.full_name}/commit/${github.context.sha})
  *Branch*: [ ${BRANCH_NAME}](https://github.com/${githubData.repository.full_name}/tree/${BRANCH_NAME})
  *History*: [ commits ](https://github.com/${githubData.repository.full_name}/commits/${github.context.sha})
  *Triggered on*: ${github.context.eventName}
  *Committer*: ${github.context.actor}
  ---------------- Commit message -------------------
  ${githubData.head_commit?.message}
  ---------------- Commit message -------------------
  `;
  console.log(botToken);
  const url: string = `https://api.telegram.org/bot${botToken}/sendMessage`;

  const formattedMessage: string = message
    .replace(/\-/gi, "\\-")
    .replace(/\./gi, "\\.")
    .replace(/\#/gi, "\\#")
    .replace(/\_/gi, "\\_");
  await axios
    .post(url, {
      chat_id: chatId,
      text: formattedMessage,
      disable_web_page_preview: disablePagePreview,
      parse_mode: parseMode,
    })
    .then((res) => console.log(res.status, res.data, res.config))
    .catch((err) => console.log(err));
}

/**
 * Identify release branch.
 * Method is looking for match of release branches,
 * if true, then this branch is the release one, and
 * message would be sent to the main release channel,
 * else false, it is not a release branch.
 *
 * @async
 * @param {string} branch - Branch name to match with.
 * @return {Promise<boolean>} True if Release Branch, else False.
 */
async function isReleaseBranch(branch: string): Promise<boolean> {
  const releaseBranches: string[] = [
    "refs/heads/master",
    "refs/heads/1.10",
    "refs/heads/2",
    "refs/tags",
  ];

  for (let releaseBranch of releaseBranches) {
    if (branch.startsWith(releaseBranch)) {
      return true;
    }
  }
  return false;
}

/**
 * Gets chat id.
 * Getting chad id of a telegram channel, which
 * will be used.
 *
 * @async
 * @param {string} branchName - Branch name to identify.
 * @return {Promise<string>} Chat Id of where message would be sent.
 */
async function getChatId(branchName: string): Promise<string> {
  const chatId = core.getInput("chat_id");
  const actor = github.context.actor;

  if (await isReleaseBranch(branchName)) {
    return chatId;
  }
  return chatId + "_" + actor;
}

/**
 * Get right branch name.
 * Parsing context reference of the GitHub,
 * to get the correct branch name. If the event
 * was not a pull request, then branch name should be
 * parsed e.g refs/head/master -> master.
 *
 * @async
 * @return {Promise<string>} Parsed branch name or unchanged.
 */
async function getBranchName(): Promise<string | undefined> {
  const branchName: string = github.context.ref;
  if (github.context.eventName != "pull_request") {
    return branchName.split("/heads/").pop();
  }
  return branchName;
}

/**
 * Gets job id of the current workflow run.
 * Method that makes the GitHub API POST request
 * via octokit (official client for the GitHub API),
 * http://octokit.github.io/ - this is a link, with the
 * documentation of how to make a request.
 *
 * @async
 * @return {Promise<number>} The job id of the workflow.
 */
async function getJobId(): Promise<any> {
  const gitHubToken = core.getInput("github_token");
  const data = github.context.payload as PushEvent;

  const octokit = github.getOctokit(gitHubToken);

  try {
    const response = await octokit.rest.actions.listJobsForWorkflowRun({
      owner: String(data.repository.owner.name),
      repo: String(data.repository.name),
      run_id: github.context.runId,
    });

    return response.data.jobs[0].id;
  } catch (err: any) {
    console.log(err.message);
  }
}

run();
