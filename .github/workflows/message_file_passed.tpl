✅ Build #{{build.number}} of `{{repo.name}}` succeeded.

GITHUB_REPOSITORY	{{repo}}
GITHUB_ACTOR	{{repo.namespace}}
GITHUB_SHA	{{commit.sha}}
GITHUB_REF	{{commit.ref}}
GITHUB_WORKFLOW	{{github.workflow}}
GITHUB_ACTION	{{github.action}}
GITHUB_EVENT_NAME	{{github.event.name}}
GITHUB_EVENT_PATH	{{github.event.path}}
GITHUB_WORKSPACE	{{github.workspace}}



📝 Commit by {{commit.author}} on `{{commit.branch}}`:

```
{{commit.message}}
```


🌐 {{ build.link }}
