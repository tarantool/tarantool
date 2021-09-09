import * as core from "@actions/core";
import * as github from "@actions/github";
import { PushEvent } from "@octokit/webhooks-definitions/schema";
import axios from "axios";

async function run(): Promise<void> {
  try {
    core.info("Starting process...");
    const botToken: string = core.getInput("token");
    const branchName: string = github.context.ref;

    const chatId: string | number = await createChatId(branchName);
    const parseMode: string = core.getInput("parse_mode");
    const disablePagePreview: boolean = core.getBooleanInput(
      "disable_web_page_preview"
    );

    await sendMessage(botToken, chatId, parseMode, disablePagePreview);
  } catch (error: any) {
    core.setFailed(error.message);
  }
}

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
  *Logs*: [ ${JOB_ID} ](https://github.com/${githubData.repository.full_name}/runs/${JOB_ID})
  *Commit*: [ ${github.context.sha} ](https://github.com/${githubData.repository.full_name}/commit/${github.context.sha})
  *Branch*: [ ${BRANCH_NAME}](https://github.com/${githubData.repository.full_name}/tree/${BRANCH_NAME})
  *History*: [ commits ](https://github.com/${githubData.repository.full_name}/commits/${github.context.sha})
  *Triggered on*: ${github.context.eventName}
  *Committer*: ${github.context.actor}
  ---------------- Commit message -------------------
  *${githubData.head_commit}*
  ---------------- Commit message -------------------
  `;
  const url: string = `https://api.telegram.org/bot${botToken}/sendMessage`;

  const formattedMessage: string = message
    .replace(/\-/gi, "\\-")
    .replace(/\./gi, "\\.")
    .replace(/\_/gi, "\\_");
  await axios
    .post(url, {
      chat_id: chatId,
      text: formattedMessage,
      disable_web_page_preview: disablePagePreview,
      parse_mode: parseMode,
    })
    .then((res) => console.log(res.status, res.data, res.config))
    .catch((err) => console.log(err.message));
}

async function createChatId(branchName: string): Promise<string> {
  const chatId = core.getInput("to");
  const actor = github.context.actor;

  const branchesToIgnore: Array<boolean> = [
    branchName.startsWith("refs/heads/master"),
    branchName.startsWith("refs/heads/1.10"),
    branchName.startsWith("refs/heads/2"),
    branchName.startsWith("refs/tags"),
  ];

  if (!branchesToIgnore) {
    return chatId + "_" + actor;
  }
  return chatId;
}

async function getBranchName(): Promise<string | undefined> {
  const branchName: string = github.context.ref;
  if (github.context.eventName != "pull_request") {
    return branchName.split("/").pop();
  }
  return branchName;
}

async function getJobId() {
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
