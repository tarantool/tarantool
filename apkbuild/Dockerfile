FROM alpine:3.5

RUN apk update \
	&& apk add alpine-sdk \
	&& adduser -G abuild -g "Alpine Package Builder" -s /bin/sh -D builder \
	&& echo "builder ALL=(ALL) NOPASSWD:ALL" >> /etc/sudoers

USER builder

RUN sudo mkdir -p /var/cache/distfiles \
	&& sudo chmod a+w /var/cache/distfiles \
	&& sudo chgrp abuild /var/cache/distfiles \
	&& sudo chmod g+w /var/cache/distfiles \
	&& abuild-keygen -a -i

COPY --chown=builder:abuild . /home/builder/tarantool/

WORKDIR /home/builder/tarantool/apkbuild

VOLUME ["/target"]

CMD ["abuild", "-r", "-P", "/target"]