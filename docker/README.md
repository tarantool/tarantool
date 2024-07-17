# Tarantool Docker Image (version 3.0+)

- [What is Tarantool](#what-is-tarantool)
- [Quick start](#quick-start)
- [What's on board](#whats-on-board)
  - [Data directories](#data-directories)
  - [Convenience tools](#convenience-tools)
- [How to use an image](#how-to-use-an-image)
  - [Start a Tarantool instance](#start-a-tarantool-instance)
  - [Connect to a running Tarantool instance](#connect-to-a-running-tarantool-instance)
  - [Add application code with a volume mount](#add-application-code-with-a-volume-mount)
  - [Build your own image](#build-your-own-image)
- [Release policy](#release-policy)
- [Reporting problems and getting help](#reporting-problems-and-getting-help)

## What is Tarantool

Tarantool is an in-memory computing platform that combines a Lua application
server and a database management system. Read more about Tarantool at
[tarantool.io](https://www.tarantool.io/en/developers/).

## Quick start

To try out Tarantool, run this command:

```console
$ docker run --rm -t -i tarantool/tarantool -i
```

It will create a one-off Tarantool instance and open an interactive console.
From there, you can either type `tutorial()` in the console or follow the
[documentation](https://www.tarantool.io/en/doc/latest/getting_started/getting_started_db/).

## What's on board

The `tarantool/tarantool` image contains the `tarantool` and `tt` executables.

There are also a few [convenience tools](#convenience-tools) that make use of
the fact that there is only one Tarantool instance running in the container.

The Docker image comes in one flavor, based on the `ubuntu:22.04` image.
The image is built and published on Docker Hub for each Tarantool release as
well as for alpha, beta, and release candidate versions.

### Data directories

Mount these directories as volumes:

- `/var/lib/tarantool` contains operational data (snapshots, xlogs, etc.).

- `/opt/tarantool` is the directory for the Lua application code.

### Convenience tools

- `console` -- opens an administrative console to a running Tarantool instance.

- `status` -- returns `running` output and zero status code if Tarantool has
  been initialized and operates normally. Otherwise, `status` returns the `not running`
  output and a non-zero status code.

## How to use an image

### Start a Tarantool instance

```console
$ docker run \
  --name app-instance-001 \
  -p 3301:3301 -d \
  tarantool/tarantool
```

This will start an instance of Tarantool and expose it on port 3301. Note that
by default there is no password protection, so don't expose this instance to the
outside world.

### Connect to a running Tarantool instance

```console
$ docker exec -t -i app-instance-001 console
```

This will open an interactive admin console on the running container named
`app-instance-001`. You can safely detach from it anytime, the server will
continue running.

This `console` doesn't require authentication, because it uses a local UNIX
domain socket in the container to connect to Tarantool. However, it requires
you to have direct access to the container.

If you need to access a remote console via TCP/IP, use the `tt` utility as
explained [here](https://www.tarantool.io/en/doc/latest/reference/tooling/tt_cli/).

### Add application code with a volume mount

The simplest way to provide application code is to mount your code directory to
`/opt/tarantool`:

```console
$ docker run \
  --name app-instance-001 \
  -p 3301:3301 -d \
  -v /path/to/my/app/instances.enabled:/opt/tarantool \
  -e TT_APP_NAME=app \
  -e TT_INSTANCE_NAME=instance-001 \
  tarantool/tarantool
```

Here, `/path/to/my/app` is the host directory containing an application. Its
content may be, for example, the following:

```console
+-- bin
+-- distfiles
+-- include
+-- instances.enabled
|   +-- app
|       +-- config.yaml
|       +-- init.lua
|       +-- instances.yaml
|       +-- app-scm-1.rockspec
+-- modules
+-- templates
+-- tt.yaml
```

See the [Creating and developing an application][develop-app] guide for details.

[develop-app]: https://www.tarantool.io/en/doc/latest/book/admin/instance_config/#admin-instance-config-develop-app

### Build your own image

To pack and distribute an image with your code, create your own `Dockerfile`:

```dockerfile
FROM tarantool/tarantool:3.1.0
COPY instances.enabled /opt/tarantool
ENV TT_APP_NAME=app
CMD ["tarantool"]
```

Then build it with:

```console
$ docker build -t company/app:tag .
```

And run a Tarantool instance:

```console
$ docker run \
  --name app-instance-001 \
  -p 3301:3301 -d \
  -e TT_INSTANCE_NAME=instance-001 \
  company/app:tag
```

We recommend building from an image with a precise tag, for example, `3.1.0`,
not `3.1` or `latest`. This way you will have more control over the updates of
Tarantool and other dependencies of your application.

## Release policy

All images are pushed to [Docker Hub](https://hub.docker.com/).

Patch-version tags (`x.y.z`) are always frozen and never updated.
Minor-version tags (`x.y`) are re-pushed if a new patch-version release has been
created.
Major-version tags (`x`) are re-pushed if a new patch-version or minor-version
release has been created.

### Example

Let's say we have the `3.1.0` release, and it is the latest release of Tarantool.
According to this fact, we have the image on Docker Hub with three tags:
`3.1.0`, `3.1`, and `3`.

If the `3.1.1` release is created, the corresponding image will be pushed to Docker
Hub with tags `3.1.1`, `3.1`, `3`. In this case, tags `3.1` and `3` will be updated.

If the `3.2.0` release is created, the corresponding image will be pushed to Docker Hub
with the tags `3.2.0`, `3.2`, and `3`, thereby updating the tag `3`.

## Reporting problems and getting help

You can report problems and request features
[on our GitHub](https://github.com/tarantool/tarantool).

Alternatively, you may get help on our
[Telegram channel](https://t.me/tarantool).
