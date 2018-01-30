Building apk package
---

```
git clone https://github.com/tarantool/tarantool.git
docker build -t apkbuild -f apkbuild/Dockerfile .
docker run --rm -it -v $(pwd)/target:/target apkbuild:latest
```

Then you will find newly created package under `./target/tarantool/<arch>` dir